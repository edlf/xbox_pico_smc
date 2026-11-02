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

struct Record {
  uint32_t magic;
  uint16_t version;
  uint16_t size;            // sizeof(Record)
  char     firmware[4];     // SMC_FIRMWARE_NAME: the thresholds differ per firmware
  uint8_t  led_brightness;  // Percent
  uint8_t  threshold_count;
  uint8_t  thresholds[Fan::max_thresholds];
  char     wifi_ssid[max_ssid_length + 1];          // Zero-terminated
  char     wifi_password[max_password_length + 1];  // Zero-terminated, empty: made from the board ID
  uint8_t  web_enabled;                             // Start the web interface (ignored without one in the build)
  uint32_t checksum;                                // FNV-1a over everything above
};
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "One flash page");

static Record        saved;  // What's in flash (or the defaults)
static bool          saved_valid = false;
static volatile bool save_requested = false;  // requestSave(), done by update()
static bool          usable = false;          // The sector is past the end of the firmware

// Wi-Fi access point, SMC_WIFI_SSID / SMC_WIFI_PASSWORD (src/config.hpp) unless saved
static char wifi_ssid[max_ssid_length + 1] = SMC_WIFI_SSID;
static char wifi_password[max_password_length + 1] = SMC_WIFI_PASSWORD;
static bool web_enabled = true;

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
  memset(&r, 0, sizeof(r));
  r.magic = magic;
  r.version = version;
  r.size = sizeof(Record);
  firmwareName(r.firmware);
  r.led_brightness = hal::get_led_brightness();
  r.threshold_count = Fan::thresholdCount();
  for (uint8_t i = 0; i < r.threshold_count && i < Fan::max_thresholds; i++) {
    r.thresholds[i] = Fan::getThreshold(i);
  }
  strncpy(r.wifi_ssid, wifi_ssid, max_ssid_length);
  strncpy(r.wifi_password, wifi_password, max_password_length);
  r.web_enabled = web_enabled;
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
  return wifi_ssid;
}

const char* wifiPassword() {
  return wifi_password;
}

bool setWifiSsid(const char* ssid) {
  const size_t length = strlen(ssid);
  if (length < 1 || length > max_ssid_length || !printable(ssid, length)) {
    return false;
  }
  memcpy(wifi_ssid, ssid, length + 1);
  return true;
}

bool setWifiPassword(const char* password) {
  const size_t length = strlen(password);
  if ((length != 0 && length < min_password_length) || length > max_password_length || !printable(password, length)) {
    return false;
  }
  memcpy(wifi_password, password, length + 1);
  return true;
}

void resetWifi() {
  setWifiSsid(SMC_WIFI_SSID);
  setWifiPassword(SMC_WIFI_PASSWORD);
}

bool webEnabled() {
  return web_enabled;
}

void setWebEnabled(const bool enabled) {
  web_enabled = enabled;
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
    memcpy(text, stored->wifi_ssid, max_ssid_length);
    text[max_ssid_length] = '\0';
    if (setWifiSsid(text)) {
      msg += ", Wi-Fi name";
    }
    memcpy(text, stored->wifi_password, max_password_length);
    text[max_password_length] = '\0';
    if (setWifiPassword(text)) {
      msg += ", Wi-Fi password";
    }
    web_enabled = stored->web_enabled != 0;
    msg += web_enabled ? ", web interface on" : ", web interface off";
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

static bool save(const Record& r) {
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
  return save(now);
}

void requestSave() {
  save_requested = true;
}

void update() {
  if (!save_requested || !usable || !saved_valid) {
    return;
  }
  save_requested = false;
  const Record now = current();
  if (sameSettings(now, saved)) {
    debug::print_message("SETTINGS: Nothing changed, not saved");
    return;
  }
  save(now);
}

}  // namespace Settings
}  // namespace SMC
