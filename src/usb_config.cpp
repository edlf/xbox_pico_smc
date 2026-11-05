#include "usb_config.hpp"
#include "usb_commands.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "pin_assignments.hpp"

#include "hardware/gpio.h"
#include "hardware/watchdog.h"
#include "pico/bootrom.h"
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
    return false;  // In the Xbox
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

void run() {
  debug::print(debug::Level::Info, "USB: Config mode, the SMC is not running");
  SMC::Settings::load();

  char            line[80];
  size_t          length = 0;
  absolute_time_t next_check = make_timeout_time_ms(standby_check_ms);
  while (true) {
    const int c = getchar_timeout_us(10000);
    if (c == '\r' || c == '\n') {
      line[length] = '\0';
      const Action action = command(line, [](const char* text) { fputs(text, stdout); });
      length = 0;
      if (action != Action::none) {
        stdio_flush();  // The OK out first
        sleep_ms(100);
        if (action == Action::bootloader) {
          reset_usb_boot(0, 0);  // Shows up as the RPI-RP2 / RP2350 drive for a UF2 file
        }
        watchdog_reboot(0, 0, 0);
        while (true) {
        }
      }
    } else if (c >= 0 && length < sizeof(line) - 1) {
      line[length++] = static_cast<char>(c);
    }

    // Put in the Xbox (or its standby power came on) after all: run the SMC
    if (time_reached(next_check)) {
      next_check = make_timeout_time_ms(standby_check_ms);
      if (standbyPower()) {
        debug::print(debug::Level::Warnings, "USB: Xbox standby power seen, restarting as the SMC");
        stdio_flush();
        watchdog_reboot(0, 0, 0);
        while (true) {
        }
      }
    }
  }
}

}  // namespace UsbConfig
