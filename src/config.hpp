#ifndef CONFIG_HPP
#define CONFIG_HPP

// Build options. Most are set by CMake (see CMakeLists.txt for what each one
// does and the values it accepts); their defaults here are only used when a
// file is built without CMake's definitions.

// The LED brightness and the Wi-Fi access point name and password are only
// set here: they're the defaults, the saved settings (src/smc_settings.cpp,
// set over USB) replace them.

// PICO_BOARD this was built for
#ifndef SMC_BOARD
#define SMC_BOARD "unknown"
#endif

// Debug level, the index into debug::DebugLevel
#ifndef SMC_DEBUG_LEVEL
#define SMC_DEBUG_LEVEL 3  // Info
#endif

// Firmware to behave like (SMC_FIRMWARE): P01, P11, D01, DXF or P2L
#ifndef SMC_FIRMWARE_NAME
#define SMC_FIRMWARE_NAME "P11"
#endif

#ifndef SMC_FIRMWARE_P11
#define SMC_FIRMWARE_P11 1  // P11 changes (also on for P2L)
#endif

#ifndef SMC_FIRMWARE_NO_CHALLENGE
#define SMC_FIRMWARE_NO_CHALLENGE 0  // Debug kits (D01, DXF): no boot challenge
#endif

#ifndef SMC_FIRMWARE_DXF
#define SMC_FIRMWARE_DXF 0  // DXF changes on top of D01
#endif

#ifndef SMC_FIRMWARE_P2L
#define SMC_FIRMWARE_P2L 0  // P2L changes on top of P11
#endif

// Version reported on SMBus 0x01 (SMC_REPORT_VERSION): "P01", "P11", "D01",
// "DXF" or "P2L"
#ifndef SMC_VERSION_STRING
#define SMC_VERSION_STRING "P11"
#endif

// Support a DVD drive. Without it there are no tray state machines, and the
// tray is reported as closed and empty.
#ifndef SMC_DVD_SUPPORT
#define SMC_DVD_SUPPORT 1
#endif

// Reset the Xbox when the boot challenge fails. Without it the challenge is
// still served over SMBus, but a failure never resets the Xbox.
#ifndef SMC_BOOT_CHALLENGE
#define SMC_BOOT_CHALLENGE 0
#endif

// Report a fixed AV pack code (SMC_AV_PACK_MODE) instead of the one read from
// the AV connector
#ifndef SMC_AV_PACK_OVERRIDE
#define SMC_AV_PACK_OVERRIDE 0
#endif

#ifndef SMC_AV_PACK_MODE
#define SMC_AV_PACK_MODE 1  // High Definition AV Pack
#endif

// Front panel LED brightness in percent (1-100) at start-up, the PIC only has
// on/off. The web interface and the USB config can change it (and save it).
#define SMC_LED_BRIGHTNESS 100
static_assert(SMC_LED_BRIGHTNESS >= 1 && SMC_LED_BRIGHTNESS <= 100, "SMC_LED_BRIGHTNESS must be 1-100");

// Fade the LEDs at power up and down
#ifndef SMC_LED_FADE
#define SMC_LED_FADE 1
#endif

// Read an NTC thermistor on GP28 (ADC2): NTC to ground, SMC_NTC_SERIES_R
// from 3.3 V to the pin. Beta equation with SMC_NTC_BETA and the resistance at
// 25 °C (SMC_NTC_R25).
#ifndef SMC_NTC_SENSOR
#define SMC_NTC_SENSOR 0
#endif

#ifndef SMC_NTC_BETA
#define SMC_NTC_BETA 3950
#endif

#ifndef SMC_NTC_R25
#define SMC_NTC_R25 10000  // Ohms
#endif

#ifndef SMC_NTC_SERIES_R
#define SMC_NTC_SERIES_R 10000  // Ohms
#endif

// Listen to the whole SMBus: log the video encoder found (writes at DEBUG)
#ifndef SMC_I2C_SNIFFER
#define SMC_I2C_SNIFFER 1
#endif

// WS2812 RGB LED on GP27 showing the boot phases
#ifndef SMC_RGB_LED
#define SMC_RGB_LED 1
#endif

// Wi-Fi access point with a status and control page, served from core 1
// (W boards only)
#ifndef SMC_WEB_INTERFACE
#define SMC_WEB_INTERFACE 0
#endif

// Access point name, unless one is saved
#define SMC_WIFI_SSID "OG-SMC"
// Access point WPA2 password, 8-63 characters, unless one is saved. Empty: one
// made from the board's unique ID ("ogsmc-<id>"), shown in the boot log.
#define SMC_WIFI_PASSWORD ""
static_assert(sizeof(SMC_WIFI_PASSWORD) == 1 || (sizeof(SMC_WIFI_PASSWORD) >= 9 && sizeof(SMC_WIFI_PASSWORD) <= 64),
              "SMC_WIFI_PASSWORD must be 8-63 characters, or empty for one made from the board's unique ID");

#endif  // CONFIG_HPP
