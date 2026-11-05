#include "usb_commands.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "git_version.h"  // Generated on every build, see cmake/git_version.cmake

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace UsbConfig {

static void (*out)(const char* text);  // command()'s, for the reply

__attribute__((format(printf, 1, 2))) static void reply(const char* format, ...) {
  char    text[160];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  out(text);
}

// Replies end with a line "OK" or "ERR <reason>", the script reads up to it
static void ok() {
  out("OK\n");
}

__attribute__((format(printf, 1, 2))) static void error(const char* format, ...) {
  char    text[160];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  reply("ERR %s\n", text);
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

// "on" or "off"; false for anything else
static bool parseOnOff(const char* text, bool& on) {
  if (strcmp(text, "on") == 0 || strcmp(text, "off") == 0) {
    on = strcmp(text, "on") == 0;
    return true;
  }
  return false;
}

// `set <name> on|off`. set() returns why it's refused, nullptr when it's done.
struct OnOffSetting {
  const char* name;
  const char* (*set)(bool on);
};

constexpr OnOffSetting on_off_settings[] = {
    {"fade",
     [](bool on) -> const char* {
       SMC::Led::setFade(on);
       return nullptr;
     }},
    {"web",
     [](bool on) -> const char* {
       SMC::Settings::setWebEnabled(on);
       return nullptr;
     }},
    {"boot-challenge",
     [](bool on) -> const char* {
       SMC::BootChallenge::setEnforced(on);
       return nullptr;
     }},
    {"no-dvd",
     [](bool on) -> const char* {
       SMC::Dvd::setSupported(!on);
       return nullptr;
     }},
    {"sniffer",
     [](bool on) -> const char* {
       SMC::I2CSniffer::setEnabled(on);
       return nullptr;
     }},
    {"sensor-bypass",
     [](bool on) -> const char* {
       SMC::Fan::setTemperatureBypass(on);
       return nullptr;
     }},
    {"power-button",
     [](bool on) -> const char* {
       SMC::FrontPanelSW::setPowerButtonEnabled(on);
       return nullptr;
     }},
    {"eject-button",
     [](bool on) -> const char* {
       SMC::FrontPanelSW::setEjectButtonEnabled(on);
       return nullptr;
     }},
    {"bootsel-power",
     [](bool on) -> const char* {
       if (!on && !SMC::FrontPanelSW::powerButtonEnabled()) {
         return "bootsel-power is needed while the power button is off: turn the power button on first";
       }
       SMC::FrontPanelSW::setBootselPower(on);
       return nullptr;
     }},
    {"ntc-shutdown",
     [](bool on) -> const char* {
       SMC::ExtraTemp::NtcShutdown shutdown = SMC::ExtraTemp::ntcShutdown();
       shutdown.enabled = on;
       SMC::ExtraTemp::setNtcShutdown(shutdown);
       return nullptr;
     }},
    {"ntc",
     [](bool on) -> const char* {
       SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
       ntc.enabled = on;
       SMC::ExtraTemp::setNtcConfig(ntc);  // Only the values are checked
       return nullptr;
     }},
};

// `set <name> <number>`, low to high. set() can refuse it too (false).
struct NumberSetting {
  const char* name;
  long        low;
  long        high;
  bool (*set)(long value);
};

static bool setNtc(uint32_t SMC::ExtraTemp::NtcConfig::* field, const long value) {
  SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
  ntc.*field = static_cast<uint32_t>(value);
  return SMC::ExtraTemp::setNtcConfig(ntc);
}

static bool setRamp(uint8_t SMC::Fan::Ramp::* field, const long value) {
  SMC::Fan::Ramp ramp = SMC::Fan::ramp();
  ramp.*field = static_cast<uint8_t>(value);
  return SMC::Fan::setRamp(ramp);
}

static bool setGlide(uint16_t SMC::Fan::Ramp::* field, const long value) {
  SMC::Fan::Ramp ramp = SMC::Fan::ramp();
  ramp.*field = static_cast<uint16_t>(value);
  return SMC::Fan::setRamp(ramp);
}

static bool setOffset(int8_t SMC::Fan::BypassOffsets::* field, const long value) {
  SMC::Fan::BypassOffsets offsets = SMC::Fan::bypassOffsets();
  offsets.*field = static_cast<int8_t>(value);
  return SMC::Fan::setBypassOffsets(offsets);
}

constexpr long max_offset = SMC::Fan::max_bypass_offset;

constexpr NumberSetting number_settings[] = {
    {"brightness",          1,                                100,
     [](long v) {
       hal::set_led_brightness(static_cast<uint8_t>(v));
       return true;
     }                                                                                                                                                                },
    {"rgb-brightness",      1,                                100,
     [](long v) {
       SMC::RgbLed::setBrightness(static_cast<uint8_t>(v));
       return true;
     }                                                                                                                                                                },
    {"fan-ramp-up",         1,                                5,                                [](long v) { return setRamp(&SMC::Fan::Ramp::up, v); }                },
    {"fan-ramp-down",       1,                                5,                                [](long v) { return setRamp(&SMC::Fan::Ramp::down, v); }              },
    {"fan-glide-up",        0,                                1000,                             [](long v) { return setGlide(&SMC::Fan::Ramp::glide_up_ms, v); }      },
    {"fan-glide-down",      0,                                1000,                             [](long v) { return setGlide(&SMC::Fan::Ramp::glide_down_ms, v); }    },
    {"bypass-cpu-offset",   -max_offset,                      max_offset,                       [](long v) { return setOffset(&SMC::Fan::BypassOffsets::cpu, v); }    },
    {"bypass-board-offset", -max_offset,                      max_offset,                       [](long v) { return setOffset(&SMC::Fan::BypassOffsets::board, v); }  },
    {"ntc-beta",            1000,                             10000,
     [](long v) {
       SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
       ntc.beta = static_cast<uint16_t>(v);
       return SMC::ExtraTemp::setNtcConfig(ntc);
     }                                                                                                                                                                },
    {"ntc-shutdown-temp",   SMC::ExtraTemp::min_ntc_shutdown, SMC::ExtraTemp::max_ntc_shutdown,
     [](long v) {
       SMC::ExtraTemp::NtcShutdown shutdown = SMC::ExtraTemp::ntcShutdown();
       shutdown.limit_c = static_cast<uint8_t>(v);
       return SMC::ExtraTemp::setNtcShutdown(shutdown);
     }                                                                                                                                                                },
    {"ntc-r25",             100,                              1000000,                          [](long v) { return setNtc(&SMC::ExtraTemp::NtcConfig::r25, v); }     },
    {"ntc-series",          100,                              1000000,                          [](long v) { return setNtc(&SMC::ExtraTemp::NtcConfig::series_r, v); }},
};

static void printHelp() {
  out("get                          settings\n"
      "set brightness <1-100>       LED brightness, percent\n"
      "set fade <on|off>            fade the LEDs at power up and down\n"
      "set rgb-mode <off|boot|mirror>  RGB LED: off, boot phases, or boot then the front panel\n"
      "set rgb-brightness <1-100>   RGB LED brightness, percent\n"
      "set threshold <index> <C>    fan threshold, min to default\n"
      "set fan-ramp-up <1-5>        fan steps up per decision, times (warm +1, hot +3)\n"
      "set fan-ramp-down <1-5>      fan steps down per cool decision\n"
      "set fan-glide-up <0-1000>    ms per speed step while speeding up, 0: at once\n"
      "set fan-glide-down <0-1000>  ms per speed step while slowing down, 0: at once\n"
      "set sensor-bypass <on|off>   CPU temperature from the NTC, board from the Pico, no ADM1032\n"
      "set bypass-cpu-offset <-30-30>    added to the NTC for the CPU temperature (C)\n"
      "set bypass-board-offset <-30-30>  added to the Pico for the board temperature (C)\n"
      "set power-button <on|off>    the front panel power button (off: BOOTSEL is the power button)\n"
      "set eject-button <on|off>    the front panel eject button\n"
      "set bootsel-power <on|off>   the Pico's BOOTSEL button as a power button too\n"
      "set ssid <name>              Wi-Fi access point name, 1-32 characters\n"
      "set password [<password>]    Wi-Fi password, 8-63 characters, none: from the board ID\n"
      "set web <on|off>             start the web interface (W boards)\n"
      "set boot-challenge <on|off>  reset the Xbox when the boot challenge fails\n"
      "set av-pack <detect|0-6>     read the AV connector, or report this AV pack code\n"
      "set log-level <silent|critical|warnings|info|debug>  console log level\n"
      "set no-dvd <on|off>          no DVD drive (on: tray reported closed and empty)\n"
      "set sniffer <on|off>         log the video encoder the CPU talks to\n"
      "set ntc <on|off>             NTC thermistor on GP28\n"
      "set ntc-beta <1000-10000>    NTC beta (K)\n"
      "set ntc-r25 <100-1000000>    NTC resistance at 25 C (ohms)\n"
      "set ntc-series <100-1000000> series resistor from 3.3 V (ohms)\n"
      "set ntc-shutdown <on|off>    shut down when the NTC reaches ntc-shutdown-temp (not with the sensor bypass)\n"
      "set ntc-shutdown-temp <40-120>  that limit (C)\n"
      "set report-version <auto|P01|P11|D01|DXF|P2L>  version reported on SMBus 0x01\n"
      "defaults                     back to the firmware's defaults\n"
      "save                         write them to flash\n"
      "reboot                       restart the Pico\n"
      "bootloader                   restart into the USB bootloader (BOOTSEL), for a firmware update\n");
}

static void printSettings() {
  reply("firmware %s\n", SMC_FIRMWARE_NAME);
  reply("git %s\n", SMC_GIT_HASH);  // Short hash of the build, "-dirty" with uncommitted changes
  SMC::Settings::describe([](const char* line) { reply("%s\n", line); });
}

// `set <name> <value>` for the settings that aren't on/off or a plain number
static void setOther(const char* name, const char* value) {
  long a;
  if (strcmp(name, "report-version") == 0) {
    if (!SMC::Settings::setReportedVersion(value)) {
      error("report-version must be auto, P01, P11, D01, DXF or P2L");
      return;
    }
  } else if (strcmp(name, "av-pack") == 0) {
    if (strcmp(value, "detect") == 0) {
      SMC::Video::setAvPackOverride(-1);
    } else if (!parseNumber(value, a) || !SMC::Video::setAvPackOverride(static_cast<int>(a))) {
      error("av-pack must be detect or 0-6");
      return;
    }
  } else if (strcmp(name, "log-level") == 0) {
    debug::Level level;
    if (!debug::parseLevel(value, level)) {
      error("log-level must be silent, critical, warnings, info or debug");
      return;
    }
    debug::setLevel(level);
  } else if (strcmp(name, "rgb-mode") == 0) {
    SMC::RgbLed::Mode mode;
    if (!SMC::RgbLed::parseMode(value, mode)) {
      error("rgb-mode must be off, boot or mirror");
      return;
    }
    SMC::RgbLed::setMode(mode);
  } else {
    error("unknown command, try help");
    return;
  }
  ok();
}

// `set <name> <value>`
static void set(const char* name, const char* value) {
  for (const OnOffSetting& setting : on_off_settings) {
    if (strcmp(name, setting.name) == 0) {
      bool on;
      if (!parseOnOff(value, on)) {
        error("%s must be on or off", name);
        return;
      }
      if (const char* refused = setting.set(on)) {
        error("%s", refused);
        return;
      }
      ok();
      return;
    }
  }
  for (const NumberSetting& setting : number_settings) {
    if (strcmp(name, setting.name) == 0) {
      long number;
      if (!parseNumber(value, number) || number < setting.low || number > setting.high || !setting.set(number)) {
        error("%s must be %ld to %ld", name, setting.low, setting.high);
        return;
      }
      ok();
      return;
    }
  }
  setOther(name, value);
}

Action command(char* line, void (*output)(const char* text)) {
  out = output;

  // The Wi-Fi name and password take the rest of the line, spaces included
  if (strncmp(line, "set ssid ", 9) == 0) {
    if (!SMC::Settings::setWifiSsid(line + 9)) {
      error("Wi-Fi name must be 1-32 printable characters");
      return Action::none;
    }
    ok();
    return Action::none;
  }
  if (strcmp(line, "set password") == 0 || strncmp(line, "set password ", 13) == 0) {
    // "set password" alone: empty, the one made from the board ID
    if (!SMC::Settings::setWifiPassword(strlen(line) == 12 ? "" : line + 13)) {
      error("Wi-Fi password must be 8-63 printable characters, or empty for the board ID one");
      return Action::none;
    }
    ok();
    return Action::none;
  }

  char*   words[4] = {};
  uint8_t count = 0;
  for (char* word = strtok(line, " \t"); word != nullptr && count < 4; word = strtok(nullptr, " \t")) {
    words[count++] = word;
  }
  if (count == 0) {
    return Action::none;
  }

  long a, b;
  if (strcmp(words[0], "help") == 0) {
    printHelp();
    ok();
  } else if (strcmp(words[0], "get") == 0) {
    printSettings();
    ok();
  } else if (strcmp(words[0], "set") == 0 && count == 3) {
    set(words[1], words[2]);
  } else if (strcmp(words[0], "set") == 0 && count == 4 && strcmp(words[1], "threshold") == 0) {
    if (!parseNumber(words[2], a) || !parseNumber(words[3], b) || a < 0 || a > 255 || b < 0 || b > 255 ||
        !SMC::Fan::setThreshold(static_cast<uint8_t>(a), static_cast<uint8_t>(b))) {
      error("no such threshold, or out of range (min to default)");
      return Action::none;
    }
    ok();
  } else if (strcmp(words[0], "defaults") == 0) {
    SMC::Settings::resetToDefaults();
    ok();
  } else if (strcmp(words[0], "save") == 0) {
    if (!SMC::Settings::saveNow()) {
      error("save failed");
      return Action::none;
    }
    ok();
  } else if (strcmp(words[0], "reboot") == 0) {
    ok();
    return Action::reboot;
  } else if (strcmp(words[0], "bootloader") == 0) {
    // Only here, in USB config mode: the Pico is out of the Xbox and a PC is
    // talking to it. Nothing on the SMBus or the web interface can get here.
    debug::print(debug::Level::Info, "USB: Restarting into the bootloader");
    ok();
    return Action::bootloader;
  } else {
    error("unknown command, try help");
  }
  return Action::none;
}

}  // namespace UsbConfig
