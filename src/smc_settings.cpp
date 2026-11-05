#include "smc_settings.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "pico/unique_id.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "text.hpp"

namespace SMC {
namespace Settings {

constexpr uint32_t magic = 0x534D4353;  // "SCMS"
constexpr uint16_t version = 1;         // Bump when the layout changes: other versions are ignored (see reserved)
constexpr uint32_t flash_offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

// The settings kept here rather than by their module, stored as they are.
// Wi-Fi access point: SMC_WIFI_SSID / SMC_WIFI_PASSWORD (src/config.hpp) unless saved.
struct Values {
  char    wifi_ssid[max_ssid_length + 1] = SMC_WIFI_SSID;              // Zero-terminated
  char    wifi_password[max_password_length + 1] = SMC_WIFI_PASSWORD;  // Zero-terminated, empty: made from the board ID
  uint8_t web_enabled = SMC_WEB_INTERFACE_DEFAULT;                     // Start the web interface (ignored without one in the build)
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
  uint8_t  rgb_brightness;       // Percent
  uint8_t  rgb_mode;             // RgbLed::Mode
  char     report_version[4];    // SMBus 0x01, empty: the build's
  uint8_t  led_fade;             // Led::fadeEnabled()
  uint8_t  boot_challenge;       // BootChallenge::enforced()
  int8_t   av_pack;              // Video::avPackOverride(): -1 reads the connector
  uint8_t  debug_level;          // debug::Level
  uint8_t  dvd_support;          // Dvd::supported()
  uint8_t  i2c_sniffer;          // I2CSniffer::enabled()
  uint8_t  ntc_enabled;          // ExtraTemp::ntcConfig()
  uint16_t ntc_beta;             // K
  uint32_t ntc_r25;              // Ohms
  uint32_t ntc_series_r;         // Ohms
  uint8_t  fan_ramp_up;          // Fan::ramp()
  uint8_t  fan_ramp_down;        //
  uint16_t fan_glide_up_ms;      //
  uint16_t fan_glide_down_ms;    //
  uint8_t  sensor_bypass;        // Fan::temperatureBypass()
  uint8_t  power_button;         // FrontPanelSW::powerButtonEnabled()
  uint8_t  eject_button;         // FrontPanelSW::ejectButtonEnabled()
  uint8_t  bootsel_power;        // As set: FrontPanelSW::bootselPowerSetting()
  int8_t   bypass_cpu_offset;    // Fan::bypassOffsets()
  int8_t   bypass_board_offset;  //
  uint8_t  ntc_shutdown;         // ExtraTemp::ntcShutdown(): 0 the default, 1 off, 2 on
  uint8_t  ntc_shutdown_temp;    // °C, 0 the default
  // Room for new settings without a new version, so a firmware update keeps
  // the saved ones: take the bytes from the start of this, with 0 meaning the
  // default (records saved before the setting existed have 0 there), and
  // shrink it by as much. Firmware that doesn't know them ignores them.
  uint8_t  reserved[30];
  uint32_t checksum;  // FNV-1a over everything above
};
static_assert(sizeof(Record) <= FLASH_PAGE_SIZE, "One flash page");
static_assert(offsetof(Record, reserved) + sizeof(Record::reserved) == offsetof(Record, checksum),
              "No padding before the checksum: the reserved bytes end where it starts");

// The reported versions, besides the build's
static const char* const report_versions[] = {"P01", "P11", "D01", "DXF", "P2L"};

static Record        saved;  // What's in flash (or the defaults)
static bool          saved_valid = false;
static volatile bool save_requested = false;  // requestSave(), done by update()
static bool          usable = false;          // The sector is past the end of the firmware
static Values        values;                  // Live, the setters keep them valid
static bool          loaded = false;          // load() found a saved record
static char          report_version[4] = "";  // Empty: SMC_VERSION_STRING

extern "C" uint8_t __flash_binary_end;

// FNV-1a over everything but the checksum itself
static uint32_t checksum(const Record& r) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&r);
  uint32_t       hash = 2166136261u;
  for (size_t i = 0; i < offsetof(Record, checksum); i++) {
    hash = (hash ^ p[i]) * 16777619u;
  }
  return hash;
}

// A record of this layout (version and size) that checks out. Anything else,
// records of other versions included, is ignored.
static bool valid(const Record& r) {
  return r.magic == magic && r.version == version && r.size == sizeof(Record) && r.checksum == checksum(r);
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
  r.power_button = FrontPanelSW::powerButtonEnabled() ? 1 : 0;
  r.eject_button = FrontPanelSW::ejectButtonEnabled() ? 1 : 0;
  r.bootsel_power = FrontPanelSW::bootselPowerSetting() ? 1 : 0;
  r.bypass_cpu_offset = Fan::bypassOffsets().cpu;
  r.bypass_board_offset = Fan::bypassOffsets().board;
  r.ntc_shutdown = ExtraTemp::ntcShutdown().enabled ? 2 : 1;
  r.ntc_shutdown_temp = ExtraTemp::ntcShutdown().limit_c;
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
  loaded = valid(*stored);
  if (!loaded) {
    debug::print(debug::Level::Info, "SETTINGS: None saved, using the defaults");
  } else {
    debug::print(debug::Level::Info, "SETTINGS: Loaded");
    // Every value checked like a new one: anything invalid keeps the default
    hal::set_led_brightness(stored->led_brightness);
    // Thresholds only from the same firmware, setThreshold() checks each range
    if (memcmp(stored->firmware, firmware, sizeof(firmware)) == 0 && stored->threshold_count == Fan::thresholdCount()) {
      for (uint8_t i = 0; i < stored->threshold_count; i++) {
        Fan::setThreshold(i, stored->thresholds[i]);
      }
    } else {
      debug::print(debug::Level::Warnings, "SETTINGS: Fan thresholds saved for another firmware, left at the defaults");
    }
    char text[max_password_length + 1];
    memcpy(text, stored->values.wifi_ssid, max_ssid_length);
    text[max_ssid_length] = '\0';
    setWifiSsid(text);
    memcpy(text, stored->values.wifi_password, max_password_length);
    text[max_password_length] = '\0';
    setWifiPassword(text);
    setWebEnabled(stored->values.web_enabled != 0);
    RgbLed::setBrightness(stored->rgb_brightness);
    if (stored->rgb_mode < RgbLed::mode_count) {
      RgbLed::setMode(static_cast<RgbLed::Mode>(stored->rgb_mode));
    }
    Led::setFade(stored->led_fade != 0);
    char name[sizeof(stored->report_version) + 1] = {};
    memcpy(name, stored->report_version, sizeof(stored->report_version));
    setReportedVersion(name);
    BootChallenge::setEnforced(stored->boot_challenge != 0);
    Video::setAvPackOverride(stored->av_pack);
    if (stored->debug_level < debug::level_count) {
      debug::setLevel(static_cast<debug::Level>(stored->debug_level));
    }
    Dvd::setSupported(stored->dvd_support != 0);
    I2CSniffer::setEnabled(stored->i2c_sniffer != 0);
    ExtraTemp::setNtcConfig({stored->ntc_enabled != 0, stored->ntc_beta, stored->ntc_r25, stored->ntc_series_r});
    Fan::setRamp({stored->fan_ramp_up, stored->fan_ramp_down, stored->fan_glide_up_ms, stored->fan_glide_down_ms});
    Fan::setTemperatureBypass(stored->sensor_bypass != 0);
    FrontPanelSW::setPowerButtonEnabled(stored->power_button != 0);
    FrontPanelSW::setEjectButtonEnabled(stored->eject_button != 0);
    FrontPanelSW::setBootselPower(stored->bootsel_power != 0);
    Fan::setBypassOffsets({stored->bypass_cpu_offset, stored->bypass_board_offset});
    ExtraTemp::setNtcShutdown({stored->ntc_shutdown == 0 ? SMC_NTC_SHUTDOWN != 0 : stored->ntc_shutdown == 2,
                               stored->ntc_shutdown_temp == 0 ? static_cast<uint8_t>(SMC_NTC_SHUTDOWN_TEMP) : stored->ntc_shutdown_temp});
  }
  if (ExtraTemp::ntcShutdown().enabled && Fan::temperatureBypass()) {
    debug::print(debug::Level::Warnings, "SETTINGS: NTC shutdown ignored while the sensor bypass is on");
  } else if (ExtraTemp::ntcShutdown().enabled && !ExtraTemp::ntcConfig().enabled) {
    debug::print(debug::Level::Warnings, "SETTINGS: NTC shutdown on, but the NTC is off");
  }
  if (Fan::temperatureBypass() && !ExtraTemp::ntcConfig().enabled) {
    debug::print(debug::Level::Warnings, "SETTINGS: Sensor bypass without the NTC on: the fan control will see a dead sensor");
  }
  saved = current();  // What's applied now, so only real changes are written
  saved_valid = true;
}

static const char* onOff(const bool on) {
  return on ? "on" : "off";
}

__attribute__((format(printf, 2, 3))) static void emitf(void (*emit)(const char*), const char* format, ...) {
  char    line[160];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  emit(line);
}

void describe(void (*emit)(const char* line)) {
  // SMBus 0x01: "auto" while it's the build's, which follows
  emitf(emit, "report-version %s default %s", reportedVersionIsDefault() ? "auto" : reportedVersion(), SMC_VERSION_STRING);
  emitf(emit, "brightness %u", hal::get_led_brightness());
  emitf(emit, "fade %s", onOff(Led::fadeEnabled()));
  // Kept on builds without the RGB LED too, ignored there
  emitf(emit, "rgb-mode %s%s", RgbLed::modeName(RgbLed::mode()), SMC_RGB_LED ? "" : " (no RGB LED in this build, ignored)");
  emitf(emit, "rgb-brightness %u", RgbLed::brightness());
  emitf(emit, "ssid %s", wifiSsid());
  emitf(emit, "password %s", wifiPassword());  // Empty: made from the board ID
  char board_password[32];
  boardWifiPassword(board_password, sizeof(board_password));
  emitf(emit, "board-password %s", board_password);  // Used while the password is empty
  // The setting is kept on builds without a web interface, it's ignored there
  emitf(emit, "web %s%s", onOff(webEnabled()), SMC_WEB_INTERFACE_SUPPORT ? "" : " (no web interface in this build, ignored)");
  emitf(emit, "boot-challenge %s", onOff(BootChallenge::enforced()));
  if (Video::avPackOverride() >= 0) {
    emitf(emit, "av-pack %d", Video::avPackOverride());
  } else {
    emit("av-pack detect");
  }
  emitf(emit, "log-level %s", debug::levelName(debug::level()));
  emitf(emit, "no-dvd %s", onOff(!Dvd::supported()));  // On: no drive, the tray reads closed and empty
  emitf(emit, "sniffer %s", onOff(I2CSniffer::enabled()));
  const ExtraTemp::NtcConfig& ntc = ExtraTemp::ntcConfig();
  emitf(emit, "ntc %s beta %u r25 %lu series %lu", onOff(ntc.enabled), ntc.beta, static_cast<unsigned long>(ntc.r25), static_cast<unsigned long>(ntc.series_r));
  const Fan::Ramp& ramp = Fan::ramp();
  emitf(emit, "fan-ramp up %u down %u glide-up %u glide-down %u", ramp.up, ramp.down, ramp.glide_up_ms, ramp.glide_down_ms);
  emitf(emit, "sensor-bypass %s", onOff(Fan::temperatureBypass()));
  emitf(emit, "bypass-offsets cpu %d board %d", Fan::bypassOffsets().cpu, Fan::bypassOffsets().board);
  emitf(emit, "ntc-shutdown %s temp %u", onOff(ExtraTemp::ntcShutdown().enabled), ExtraTemp::ntcShutdown().limit_c);
  emitf(emit, "power-button %s", onOff(FrontPanelSW::powerButtonEnabled()));
  emitf(emit, "eject-button %s", onOff(FrontPanelSW::ejectButtonEnabled()));
  // As set; it's in use anyway while the power button is off
  emitf(emit, "bootsel-power %s", onOff(FrontPanelSW::bootselPowerSetting()));
  for (uint8_t i = 0; i < Fan::thresholdCount(); i++) {
    emitf(emit, "threshold %u %u default %u min %u label %s", i, Fan::getThreshold(i), Fan::thresholdDefault(i), Fan::min_threshold, Fan::thresholdLabel(i));
  }
}

void printSettings() {
  // Not at the quieter levels: it's long, and has the Wi-Fi password
  if (debug::level() < debug::Level::Info) {
    return;
  }
  debug::line("Settings (%s):", loaded ? "saved" : "none saved, the defaults");
  // Shorter than the `get` reply: no fan thresholds (long lines, and the USB
  // config shows them), and no empty password (board-password is the one in
  // use then)
  describe([](const char* line) {
    if (strncmp(line, "threshold ", 10) == 0 || strcmp(line, "password ") == 0) {
      return;
    }
    debug::line("  > %s", line);
  });
}

void resetToDefaults() {
  hal::set_led_brightness(SMC_LED_BRIGHTNESS);
  Led::setFade(SMC_LED_FADE);
  RgbLed::setMode(static_cast<RgbLed::Mode>(SMC_RGB_MODE));
  RgbLed::setBrightness(SMC_RGB_BRIGHTNESS);
  Fan::resetThresholds();
  resetWifi();
  setWebEnabled(SMC_WEB_INTERFACE_DEFAULT);
  setReportedVersion("auto");
  BootChallenge::setEnforced(SMC_BOOT_CHALLENGE);
  Fan::setRamp(Fan::default_ramp);
  Fan::setTemperatureBypass(false);
  Fan::setBypassOffsets(Fan::default_bypass_offsets);
  FrontPanelSW::setPowerButtonEnabled(true);
  FrontPanelSW::setEjectButtonEnabled(true);
  FrontPanelSW::setBootselPower(SMC_BOOTSEL_POWER);
  Video::setAvPackOverride(SMC_AV_PACK_OVERRIDE ? SMC_AV_PACK_MODE : -1);
  debug::setLevel(static_cast<debug::Level>(SMC_DEBUG_LEVEL));
  Dvd::setSupported(!SMC_NO_DVD);
  I2CSniffer::setEnabled(SMC_I2C_SNIFFER);
  ExtraTemp::setNtcConfig({SMC_NTC_SENSOR != 0, SMC_NTC_BETA, SMC_NTC_R25, SMC_NTC_SERIES_R});
  ExtraTemp::setNtcShutdown({SMC_NTC_SHUTDOWN != 0, SMC_NTC_SHUTDOWN_TEMP});
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
  // Stops core 1 (web interface) and the interrupts while the flash is busy.
  // Not flash_safe_execute() itself: in builds with the web interface it
  // refuses while core 1 isn't running (USB config mode, web interface off).
  const int result = hal::run_flash_safe(eraseAndProgram, const_cast<Record*>(&r), 500);
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
