#include "smc_settings.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"

#include <cstring>
#include <string>

namespace SMC {
namespace Settings {

constexpr uint32_t magic = 0x534D4353;  // "SCMS"
constexpr uint16_t version = 0;         // Bump when the layout changes, older records are ignored
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
  uint32_t checksum;  // FNV-1a over everything above
};
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "One flash page");

static Record        saved;  // What's in flash (or the defaults)
static bool          saved_valid = false;
static volatile bool save_requested = false;  // requestSave(), done by update()
static bool          usable = false;          // The sector is past the end of the firmware
static Values        values;                  // Live, the setters keep them valid

extern "C" uint8_t __flash_binary_end;

static uint32_t checksum(const Record& r) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&r);
  uint32_t       hash = 2166136261u;
  for (size_t i = 0; i < offsetof(Record, checksum); i++) {
    hash = (hash ^ p[i]) * 16777619u;
  }
  return hash;
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
  r.threshold_count = Fan::thresholdCount();
  for (uint8_t i = 0; i < r.threshold_count && i < Fan::max_thresholds; i++) {
    r.thresholds[i] = Fan::getThreshold(i);
  }
  r.values = values;
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

static bool sameSettings(const Record& a, const Record& b) {
  return memcmp(&a, &b, offsetof(Record, checksum)) == 0;
}

void load() {
  const uintptr_t binary_end = reinterpret_cast<uintptr_t>(&__flash_binary_end) - XIP_BASE;
  usable = binary_end <= flash_offset;
  if (!usable) {
    debug::print_warn("SETTINGS: Firmware reaches the settings sector, not saved");
    return;
  }

  const Record* stored = reinterpret_cast<const Record*>(XIP_BASE + flash_offset);
  char          firmware[4];
  firmwareName(firmware);
  if (stored->magic != magic || stored->version != version || stored->size != sizeof(Record) || stored->checksum != checksum(*stored)) {
    debug::print_message("SETTINGS: None saved, using the defaults");
  } else {
    hal::set_led_brightness(stored->led_brightness);
    std::string msg = "SETTINGS: Loaded, LED brightness " + std::to_string(stored->led_brightness) + "%";
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
    debug::print_message(msg);
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
    debug::print_warn("SETTINGS: Save failed (" + std::to_string(result) + ")");
    return false;
  }
  saved = r;
  debug::print_message("SETTINGS: Saved");
  return true;
}

bool saveNow() {
  if (!usable || !saved_valid) {
    return false;
  }
  const Record now = current();
  if (sameSettings(now, saved)) {
    debug::print_message("SETTINGS: Nothing changed, not saved");
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
