#include "usb_config.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "pin_assignments.hpp"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace UsbConfig {

constexpr uint32_t enumerate_timeout_ms = 1000;
constexpr uint32_t standby_check_ms = 500;

// In the Xbox, AV mode 0 and 2 are pulled up to the standby rail through 10k
// (see docs/Wiring.md), which wins over the Pico's ~50k pull-down. Out of the
// Xbox they float and the pull-down reads low. An AV cable that grounds both
// (SCART) hides the pull-ups, so this isn't proof on its own. The input buffer
// is only on for the read (RP2350-E9, see hal readWeakInput()).
static bool pulledUp(const uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_down(pin);
#if PICO_RP2350
  gpio_set_input_enabled(pin, false);
  busy_wait_us(50);
  gpio_set_input_enabled(pin, true);
  busy_wait_us(10);
  const bool level = gpio_get(pin);
  gpio_set_input_enabled(pin, false);
  return level;
#else
  busy_wait_us(50);
  return gpio_get(pin);
#endif
}

static bool standbyPower() {
  return pulledUp(pins::VIDEO_MODE_0) || pulledUp(pins::VIDEO_MODE_2);
}

bool detect() {
  if (standbyPower()) {
    return false; // In the Xbox
  }
#ifdef PICO_VBUS_PIN
  // Pico / Pico 2: no USB power, no need to wait for a host
  gpio_init(PICO_VBUS_PIN);
  gpio_set_dir(PICO_VBUS_PIN, GPIO_IN);
  if (!gpio_get(PICO_VBUS_PIN)) {
    return false;
  }
#endif
  stdio_usb_init();
  const absolute_time_t deadline = make_timeout_time_ms(enumerate_timeout_ms);
  while (!tud_mounted() && !time_reached(deadline)) {
    sleep_ms(10);
  }
  if (!tud_mounted()) {
    stdio_usb_deinit();
    return false;
  }
  return true;
}

// Replies end with a line "OK" or "ERR <reason>", the script reads up to it
static void ok() {
  printf("OK\n");
}

static void error(const char* reason) {
  printf("ERR %s\n", reason);
}

static void printSettings() {
  printf("firmware %s\n", SMC_FIRMWARE_NAME);
  printf("brightness %u\n", hal::get_led_brightness());
  printf("ssid %s\n", SMC::Settings::wifiSsid());
  printf("password %s\n", SMC::Settings::wifiPassword()); // Empty: made from the board ID
  // The setting is kept on builds without a web interface, it's ignored there
  printf("web %s%s\n", SMC::Settings::webEnabled() ? "on" : "off",
         SMC_WEB_INTERFACE ? "" : " (no web interface in this build, ignored)");
  for (uint8_t i = 0; i < SMC::Fan::thresholdCount(); i++) {
    printf("threshold %u %u default %u min %u label %s\n", i, SMC::Fan::getThreshold(i),
           SMC::Fan::thresholdDefault(i), SMC::Fan::min_threshold, SMC::Fan::thresholdLabel(i));
  }
}

// Whole decimal number, false on anything else
static bool parseNumber(const char* text, long& value) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  char* end;
  value = strtol(text, &end, 10);
  return *end == '\0';
}

static void command(char* line) {
  // The Wi-Fi name and password take the rest of the line, spaces included
  if (strncmp(line, "set ssid ", 9) == 0) {
    if (!SMC::Settings::setWifiSsid(line + 9)) {
      error("Wi-Fi name must be 1-32 printable characters");
      return;
    }
    ok();
    return;
  }
  if (strcmp(line, "set password") == 0 || strncmp(line, "set password ", 13) == 0) {
    // "set password" alone: empty, the one made from the board ID
    if (!SMC::Settings::setWifiPassword(strlen(line) == 12 ? "" : line + 13)) {
      error("Wi-Fi password must be 8-63 printable characters, or empty for the board ID one");
      return;
    }
    ok();
    return;
  }

  char* words[4] = {};
  uint8_t count = 0;
  for (char* word = strtok(line, " \t"); word != nullptr && count < 4; word = strtok(nullptr, " \t")) {
    words[count++] = word;
  }
  if (count == 0) {
    return;
  }

  long a, b;
  if (strcmp(words[0], "help") == 0) {
    printf("get                          settings\n"
           "set brightness <1-100>       LED brightness, percent\n"
           "set threshold <index> <C>    fan threshold, min to default\n"
           "set ssid <name>              Wi-Fi access point name, 1-32 characters\n"
           "set password [<password>]    Wi-Fi password, 8-63 characters, none: from the board ID\n"
           "set web <on|off>             start the web interface (W boards)\n"
           "defaults                     back to the firmware's defaults\n"
           "save                         write them to flash\n"
           "reboot                       restart the Pico\n");
    ok();
  } else if (strcmp(words[0], "get") == 0) {
    printSettings();
    ok();
  } else if (strcmp(words[0], "set") == 0 && count == 3 && strcmp(words[1], "web") == 0) {
    if (strcmp(words[2], "on") != 0 && strcmp(words[2], "off") != 0) {
      error("web must be on or off");
      return;
    }
    SMC::Settings::setWebEnabled(strcmp(words[2], "on") == 0);
    ok();
  } else if (strcmp(words[0], "set") == 0 && count == 3 && strcmp(words[1], "brightness") == 0) {
    if (!parseNumber(words[2], a) || a < 1 || a > 100) {
      error("brightness must be 1-100");
      return;
    }
    hal::set_led_brightness(a);
    ok();
  } else if (strcmp(words[0], "set") == 0 && count == 4 && strcmp(words[1], "threshold") == 0) {
    if (!parseNumber(words[2], a) || !parseNumber(words[3], b) || a < 0 || a > 255 || b < 0 || b > 255 ||
        !SMC::Fan::setThreshold(a, b)) {
      error("no such threshold, or out of range (min to default)");
      return;
    }
    ok();
  } else if (strcmp(words[0], "defaults") == 0) {
    hal::set_led_brightness(SMC_LED_BRIGHTNESS);
    SMC::Fan::resetThresholds();
    SMC::Settings::resetWifi();
    SMC::Settings::setWebEnabled(true);
    ok();
  } else if (strcmp(words[0], "save") == 0) {
    if (!SMC::Settings::saveNow()) {
      error("save failed");
      return;
    }
    ok();
  } else if (strcmp(words[0], "reboot") == 0) {
    ok();
    stdio_flush();
    sleep_ms(100);
    watchdog_reboot(0, 0, 0);
    while (true) {
    }
  } else {
    error("unknown command, try help");
  }
}

void run() {
  debug::print_message("USB: Config mode, the SMC is not running");
  SMC::Settings::load();

  char line[80];
  size_t length = 0;
  absolute_time_t next_check = make_timeout_time_ms(standby_check_ms);
  while (true) {
    const int c = getchar_timeout_us(10000);
    if (c == '\r' || c == '\n') {
      line[length] = '\0';
      command(line);
      length = 0;
    } else if (c >= 0 && length < sizeof(line) - 1) {
      line[length++] = static_cast<char>(c);
    }

    // Put in the Xbox (or its standby power came on) after all: run the SMC
    if (time_reached(next_check)) {
      next_check = make_timeout_time_ms(standby_check_ms);
      if (standbyPower()) {
        debug::print_warn("USB: Xbox standby power seen, restarting as the SMC");
        stdio_flush();
        watchdog_reboot(0, 0, 0);
        while (true) {
        }
      }
    }
  }
}

} // namespace UsbConfig
