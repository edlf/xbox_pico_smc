#ifndef SMC_SETTINGS_HPP
#define SMC_SETTINGS_HPP

#include <cstddef>

namespace SMC {
namespace Settings {

// Settings changed at runtime (web interface, USB config) kept in the last
// 4 KB sector of the Pico's flash, so they survive a restart: the LED
// brightness, the fan thresholds, and the Wi-Fi access point name and password
// (defaults SMC_WIFI_SSID / SMC_WIFI_PASSWORD in src/config.hpp). They're
// loaded at start-up, but only saved when asked to: the USB config mode's
// `save` (saveNow()), or requestSave(), which nothing calls yet. Changes are
// otherwise lost when the Pico restarts.
// requestSave() waits until the Xbox is off: erasing the sector stops both
// cores for up to a few hundred ms, too long while the SMBus and the fan
// control are running.

// At start-up, once the modules are initialised: applies what was saved
void load();
// The boot log's Settings section: every setting as it is in use now, saved or
// the defaults (named as in the USB config)
void printSettings();

// Wi-Fi access point. Read when the access point starts, at power-up: a change
// only takes effect after a save and a restart.
constexpr size_t max_ssid_length = 32;
constexpr size_t min_password_length = 8;  // WPA2
constexpr size_t max_password_length = 63;
const char*      wifiSsid();
// Empty: the web interface makes one from the board's unique ID
const char* wifiPassword();
// "smcx-<board ID>", the password used while none is saved: different for
// every Pico
void boardWifiPassword(char* out, size_t size);
// The password the access point uses: the saved one, or the board ID one.
// True when it's the board ID one.
bool accessPointPassword(char* out, size_t size);
// False (and nothing changed) if it doesn't fit: 1-32 printable characters
bool setWifiSsid(const char* ssid);
// False (and nothing changed) if it doesn't fit: empty or 8-63 printable characters
bool setWifiPassword(const char* password);
// Back to SMC_WIFI_SSID / SMC_WIFI_PASSWORD
void resetWifi();

// Start the web interface at power-up (default SMC_WEB_START, off). Kept and saved on builds
// without one too, it's only ignored there. A change takes effect after a
// save and a restart.
bool webEnabled();
void setWebEnabled(bool enabled);

// Version reported on SMBus 0x01, three characters: "P01", "P11", "D01",
// "DXF" or "P2L". SMC_VERSION_STRING (CMake SMC_REPORT_VERSION) unless one is
// set and saved. Only what's reported changes, not how the SMC behaves
// (SMC_FIRMWARE). Read from the SMBus IRQ; set only while the SMBus isn't
// running (load(), USB config mode).
const char* reportedVersion();
bool        reportedVersionIsDefault();  // Not set: the build's
// One of the names above, or "" / "auto" for the build's. False (and nothing
// changed) otherwise.
bool setReportedVersion(const char* name);
// Save the current settings, the next time update() runs (Xbox off)
void requestSave();
// Save them now, only where nothing else is running (USB config mode). False
// if it failed.
bool saveNow();
// Called once per tick while the Xbox is off
void update();

}  // namespace Settings
}  // namespace SMC

#endif  // SMC_SETTINGS_HPP
