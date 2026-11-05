#include "smc_settings.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/unique_id.h"

#include <cstdio>
#include <cstring>
#include "text.hpp"

namespace SMC {
namespace Settings {

constexpr uint32_t magic = 0x534D4353;  // "SCMS"
constexpr uint16_t version = 6;         // Bump when the layout changes (add to layouts[] below)
constexpr uint32_t flash_offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

// The settings kept here rather than by their module, stored as they are.
// Wi-Fi access point: SMC_WIFI_SSID / SMC_WIFI_PASSWORD (src/config.hpp) unless saved.
struct Values {
  char    wifi_ssid[max_ssid_length + 1] = SMC_WIFI_SSID;              // Zero-terminated
  char    wifi_password[max_password_length + 1] = SMC_WIFI_PASSWORD;  // Zero-terminated, empty: made from the board ID
  uint8_t web_enabled = 1;                                             // Start the web interface (ignored without one in the build)
};

struct Record {
  uint32_t magic;
  uint16_t version;
  uint16_t size;            // sizeof(Record)
  char     firmware[4];     // SMC_FIRMWARE_NAME: the thresholds differ per firmware
  uint8_t  led_brightness;  // Percent
  uint8_t  threshold_count;
  uint8_t  thresholds[Fan::max_thresholds];
  Values   values;
  uint8_t  rgb_brightness;     // Percent
  uint8_t  rgb_mode;           // RgbLed::Mode
  char     report_version[4];  // SMBus 0x01, empty: the build's (version 2)
  uint8_t  led_fade;           // Led::fadeEnabled() (version 3)
  uint8_t  boot_challenge;     // BootChallenge::enforced() (version 4, the rest too)
  int8_t   av_pack;            // Video::avPackOverride(): -1 reads the connector
  uint8_t  debug_level;        // debug::Level
  uint8_t  dvd_support;        // Dvd::supported()
  uint8_t  i2c_sniffer;        // I2CSniffer::enabled()
  uint8_t  ntc_enabled;        // ExtraTemp::ntcConfig()
  uint16_t ntc_beta;
  uint32_t ntc_r25;
  uint32_t ntc_series_r;
  uint8_t  fan_ramp_up;  // Fan::ramp() (version 5)
  uint8_t  fan_ramp_down;
  uint16_t fan_glide_up_ms;
  uint16_t fan_glide_down_ms;
  uint8_t  sensor_bypass;  // Fan::temperatureBypass() (version 6)
  uint32_t checksum;       // FNV-1a over everything above
};
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "One flash page");

// Each layout version adds fields at the end, so an older record is this one
// up to where its fields end, then its checksum (4-byte aligned). Older
// records still load; the fields they don't have keep their defaults.
struct Layout {
  uint16_t version;
  size_t   fields_end;
};
constexpr Layout layouts[] = {
    {1, offsetof(Record, report_version)},
    {2, offsetof(Record, led_fade)      },
    {3, offsetof(Record, boot_challenge)},
    {4, offsetof(Record, fan_ramp_up)   },
    {5, offsetof(Record, sensor_bypass) },
    {6, offsetof(Record, checksum)      },
};
static_assert(layouts[sizeof(layouts) / sizeof(layouts[0]) - 1].version == version, "The last layout is this one");

constexpr uint16_t recordSize(const size_t fields_end) {
  return static_cast<uint16_t>(((fields_end + 3) & ~size_t{3}) + 4);
}
static_assert(recordSize(offsetof(Record, checksum)) == sizeof(Record), "The checksum is the last field");

// The reported versions, besides the build's
static const char* const report_versions[] = {"P01", "P11", "D01", "DXF", "P2L"};

static Record        saved;  // What's in flash (or the defaults)
static bool          saved_valid = false;
static volatile bool save_requested = false;  // requestSave(), done by update()
static bool          usable = false;          // The sector is past the end of the firmware
static Values        values;                  // Live, the setters keep them valid
static char          report_version[4] = "";  // Empty: SMC_VERSION_STRING

extern "C" uint8_t __flash_binary_end;

// FNV-1a over the first length bytes
static uint32_t checksum(const void* data, const size_t length) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  uint32_t       hash = 2166136261u;
  for (size_t i = 0; i < length; i++) {
    hash = (hash ^ p[i]) * 16777619u;
  }
  return hash;
}

static uint32_t checksum(const Record& r) {
  return checksum(&r, offsetof(Record, checksum));
}

// Where a valid stored record's fields end (one of layouts[]), 0 for none
static size_t storedFields(const Record* stored) {
  if (stored->magic != magic) {
    return 0;
  }
  for (const Layout& layout : layouts) {
    const uint16_t size = recordSize(layout.fields_end);
    if (stored->version != layout.version || stored->size != size) {
      continue;
    }
    uint32_t stored_checksum;
    memcpy(&stored_checksum, reinterpret_cast<const uint8_t*>(stored) + size - 4, sizeof(stored_checksum));
    return stored_checksum == checksum(stored, size - 4) ? layout.fields_end : 0;
  }
  return 0;
}

// The stored record has this field (it's from this layout version or later)
static bool has(const size_t fields, const size_t field_offset) {
  return field_offset < fields;
}

static void firmwareName(char (&out)[4]) {
  memset(out, 0, sizeof(out));
  strncpy(out, SMC_FIRMWARE_NAME, sizeof(out));
}

// The settings as they are now
static Record current() {
  Record r;
  memset(static_cast<void*>(&r), 0, sizeof(r));  // Padding too, the checksum covers it
  r.magic = magic;
  r.version = version;
  r.size = sizeof(Record);
  firmwareName(r.firmware);
  r.led_brightness = hal::get_led_brightness();
  r.rgb_brightness = RgbLed::brightness();
  r.rgb_mode = static_cast<uint8_t>(RgbLed::mode());
  r.threshold_count = Fan::thresholdCount();
  for (uint8_t i = 0; i < r.threshold_count && i < Fan::max_thresholds; i++) {
    r.thresholds[i] = Fan::getThreshold(i);
  }
  r.values = values;
  memcpy(r.report_version, report_version, sizeof(r.report_version));
  r.led_fade = Led::fadeEnabled() ? 1 : 0;
  r.boot_challenge = BootChallenge::enforced() ? 1 : 0;
  r.av_pack = Video::avPackOverride();
  r.debug_level = static_cast<uint8_t>(debug::level());
  r.dvd_support = Dvd::supported() ? 1 : 0;
  r.i2c_sniffer = I2CSniffer::enabled() ? 1 : 0;
  const ExtraTemp::NtcConfig& ntc = ExtraTemp::ntcConfig();
  r.ntc_enabled = ntc.enabled ? 1 : 0;
  r.ntc_beta = ntc.beta;
  r.ntc_r25 = ntc.r25;
  r.ntc_series_r = ntc.series_r;
  const Fan::Ramp& ramp = Fan::ramp();
  r.fan_ramp_up = ramp.up;
  r.fan_ramp_down = ramp.down;
  r.fan_glide_up_ms = ramp.glide_up_ms;
  r.fan_glide_down_ms = ramp.glide_down_ms;
  r.sensor_bypass = Fan::temperatureBypass() ? 1 : 0;
  r.checksum = checksum(r);
  return r;
}

// Printable ASCII only, so it shows and types the same everywhere
static bool printable(const char* text, const size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (text[i] < 0x20 || text[i] > 0x7E) {
      return false;
    }
  }
  return true;
}

const char* wifiSsid() {
  return values.wifi_ssid;
}

const char* wifiPassword() {
  return values.wifi_password;
}

// The SDK reads the ID at boot, so this is also safe from core 1
void boardWifiPassword(char* out, const size_t size) {
  char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
  pico_get_unique_board_id_string(id, sizeof(id));
  snprintf(out, size, "smcx-%s", id);
}

bool accessPointPassword(char* out, const size_t size) {
  if (values.wifi_password[0] != '\0') {
    snprintf(out, size, "%s", values.wifi_password);
    return false;
  }
  boardWifiPassword(out, size);
  return true;
}

bool setWifiSsid(const char* ssid) {
  const size_t length = strlen(ssid);
  if (length < 1 || length > max_ssid_length || !printable(ssid, length)) {
    return false;
  }
  memset(values.wifi_ssid, 0, sizeof(values.wifi_ssid));  // Zeroed past the end, so equal names compare equal
  memcpy(values.wifi_ssid, ssid, length);
  return true;
}

bool setWifiPassword(const char* password) {
  const size_t length = strlen(password);
  if ((length != 0 && length < min_password_length) || length > max_password_length || !printable(password, length)) {
    return false;
  }
  memset(values.wifi_password, 0, sizeof(values.wifi_password));
  memcpy(values.wifi_password, password, length);
  return true;
}

void resetWifi() {
  setWifiSsid(SMC_WIFI_SSID);
  setWifiPassword(SMC_WIFI_PASSWORD);
}

bool webEnabled() {
  return values.web_enabled != 0;
}

void setWebEnabled(const bool enabled) {
  values.web_enabled = enabled ? 1 : 0;
}

const char* reportedVersion() {
  return report_version[0] != '\0' ? report_version : SMC_VERSION_STRING;
}

bool reportedVersionIsDefault() {
  return report_version[0] == '\0';
}

bool setReportedVersion(const char* name) {
  if (name[0] == '\0' || strcmp(name, "auto") == 0) {
    memset(report_version, 0, sizeof(report_version));
    return true;
  }
  for (const char* candidate : report_versions) {
    if (strcmp(name, candidate) == 0) {
      memcpy(report_version, candidate, sizeof(report_version));
      return true;
    }
  }
  return false;
}

static bool sameSettings(const Record& a, const Record& b) {
  return memcmp(&a, &b, offsetof(Record, checksum)) == 0;
}

void load() {
  const uintptr_t binary_end = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
  usable = binary_end <= flash_offset;
  if (!usable) {
    debug::print(debug::Level::Warnings, "SETTINGS: Firmware reaches the settings sector, not saved");
    return;
  }

  const Record* stored = reinterpret_cast<const Record*>(XIP_BASE + flash_offset);
  char          firmware[4];
  firmwareName(firmware);
  const size_t fields = storedFields(stored);
  if (fields == 0) {
    debug::print(debug::Level::Info, "SETTINGS: None saved, using the defaults");
  } else {
    hal::set_led_brightness(stored->led_brightness);
    Text<224> msg;
    msg.append("SETTINGS: Loaded, LED brightness %u%%", stored->led_brightness);
    // Thresholds only from the same firmware, setThreshold() checks each range
    if (memcmp(stored->firmware, firmware, sizeof(firmware)) == 0 && stored->threshold_count == Fan::thresholdCount()) {
      for (uint8_t i = 0; i < stored->threshold_count; i++) {
        Fan::setThreshold(i, stored->thresholds[i]);
      }
      msg += ", fan thresholds";
    } else {
      msg += ", fan thresholds left at the defaults (saved for another firmware)";
    }
    // Checked like a new value, the defaults stay on anything invalid
    char text[max_password_length + 1];
    memcpy(text, stored->values.wifi_ssid, max_ssid_length);
    text[max_ssid_length] = '\0';
    if (setWifiSsid(text)) {
      msg += ", Wi-Fi name";
    }
    memcpy(text, stored->values.wifi_password, max_password_length);
    text[max_password_length] = '\0';
    if (setWifiPassword(text)) {
      msg += ", Wi-Fi password";
    }
    setWebEnabled(stored->values.web_enabled != 0);
    msg += webEnabled() ? ", web interface on" : ", web interface off";
    // Checked too: an invalid value leaves the default
    RgbLed::setBrightness(stored->rgb_brightness);
    if (stored->rgb_mode < RgbLed::mode_count) {
      RgbLed::setMode(static_cast<RgbLed::Mode>(stored->rgb_mode));
    }
    msg.append(", RGB LED %s %u%%", RgbLed::modeName(RgbLed::mode()), RgbLed::brightness());
    if (has(fields, offsetof(Record, led_fade))) {
      Led::setFade(stored->led_fade != 0);
      msg += Led::fadeEnabled() ? ", LED fade on" : ", LED fade off";
    }
    // Checked too: anything else stays the build's
    if (has(fields, offsetof(Record, report_version))) {
      char name[sizeof(stored->report_version) + 1] = {};
      memcpy(name, stored->report_version, sizeof(stored->report_version));
      if (setReportedVersion(name) && !reportedVersionIsDefault()) {
        msg.append(", reported version %s", reportedVersion());
      }
    }
    debug::print(debug::Level::Info, "%s", msg.c_str());

    // Checked like new values: anything invalid keeps the build's default
    if (has(fields, offsetof(Record, boot_challenge))) {
      BootChallenge::setEnforced(stored->boot_challenge != 0);
      Video::setAvPackOverride(stored->av_pack);
      if (stored->debug_level < debug::level_count) {
        debug::setLevel(static_cast<debug::Level>(stored->debug_level));
      }
      Dvd::setSupported(stored->dvd_support != 0);
      I2CSniffer::setEnabled(stored->i2c_sniffer != 0);
      ExtraTemp::setNtcConfig({stored->ntc_enabled != 0, stored->ntc_beta, stored->ntc_r25, stored->ntc_series_r});
    }
    if (has(fields, offsetof(Record, fan_ramp_up))) {
      Fan::setRamp({stored->fan_ramp_up, stored->fan_ramp_down, stored->fan_glide_up_ms, stored->fan_glide_down_ms});
    }
    if (has(fields, offsetof(Record, sensor_bypass))) {
      Fan::setTemperatureBypass(stored->sensor_bypass != 0);
    }
    // A second line: they don't all fit in one
    Text<160> more;
    more.append("SETTINGS: Boot challenge %s", BootChallenge::enforced() ? "enforced" : "not enforced");
    if (Video::avPackOverride() >= 0) {
      more.append(", AV pack fixed at %d", Video::avPackOverride());
    } else {
      more += ", AV pack from the connector";
    }
    more.append(", log level %s, DVD drive %s, sniffer %s", debug::levelName(debug::level()), Dvd::supported() ? "on" : "off",
                I2CSniffer::enabled() ? "on" : "off");
    const ExtraTemp::NtcConfig& ntc = ExtraTemp::ntcConfig();
    if (ntc.enabled) {
      more.append(", NTC beta %u R25 %lu series %lu", ntc.beta, static_cast<unsigned long>(ntc.r25), static_cast<unsigned long>(ntc.series_r));
    } else {
      more += ", NTC off";
    }
    debug::print(debug::Level::Info, "%s", more.c_str());
    const Fan::Ramp& ramp = Fan::ramp();
    debug::print(debug::Level::Info, "SETTINGS: Fan ramp up x%u down x%u, glide up %u ms down %u ms per step", ramp.up, ramp.down, ramp.glide_up_ms,
                 ramp.glide_down_ms);
    if (Fan::temperatureBypass()) {
      debug::print(debug::Level::Warnings, "SETTINGS: Sensor bypass on: CPU temperature from the NTC, board from the Pico, ADM1032 not read");
      if (!ExtraTemp::ntcConfig().enabled) {
        debug::print(debug::Level::Warnings, "SETTINGS: Sensor bypass without the NTC on: the fan control will see a dead sensor");
      }
    }
  }
  saved = current();  // What's applied now, so only real changes are written
  saved_valid = true;
}

static void eraseAndProgram(void* param) {
  const Record* r = static_cast<const Record*>(param);
  uint8_t       page[FLASH_PAGE_SIZE];
  memset(page, 0xFF, sizeof(page));
  memcpy(page, r, sizeof(Record));
  flash_range_erase(flash_offset, FLASH_SECTOR_SIZE);
  flash_range_program(flash_offset, page, sizeof(page));
}

static bool write(const Record& r) {
  hal::petWatchdog();
  // Stops core 1 (web interface) and the interrupts while the flash is busy
  const int result = flash_safe_execute(eraseAndProgram, const_cast<Record*>(&r), 500);
  hal::petWatchdog();
  if (result != PICO_OK) {
    debug::print(debug::Level::Warnings, "SETTINGS: Save failed (%d)", result);
    return false;
  }
  saved = r;
  debug::print(debug::Level::Info, "SETTINGS: Saved");
  return true;
}

bool saveNow() {
  if (!usable || !saved_valid) {
    return false;
  }
  const Record now = current();
  if (sameSettings(now, saved)) {
    debug::print(debug::Level::Info, "SETTINGS: Nothing changed, not saved");
    return true;
  }
  return write(now);
}

void requestSave() {
  save_requested = true;
}

void update() {
  if (!save_requested) {
    return;
  }
  save_requested = false;
  saveNow();
}

}  // namespace Settings
}  // namespace SMC
