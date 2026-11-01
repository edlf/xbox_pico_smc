#ifndef CONFIG_HPP
#define CONFIG_HPP

// Build options. CMake sets all of these (see CMakeLists.txt for what each one
// does and the values it accepts); the defaults here are only used when a file
// is built without CMake's definitions, such as by an editor's code model.

// PICO_BOARD this was built for
#ifndef SMC_BOARD
#define SMC_BOARD "unknown"
#endif

// Debug level, the index into debug::DebugLevel
#ifndef SMC_DEBUG_LEVEL
#define SMC_DEBUG_LEVEL 3 // Info
#endif

// Firmware to behave like (SMC_FIRMWARE): P01, P11, D01, DXF or P2L
#ifndef SMC_FIRMWARE_NAME
#define SMC_FIRMWARE_NAME "P11"
#endif
#ifndef SMC_FIRMWARE_P11
#define SMC_FIRMWARE_P11 1 // P11 changes (also on for P2L)
#endif
#ifndef SMC_FIRMWARE_NO_CHALLENGE
#define SMC_FIRMWARE_NO_CHALLENGE 0 // Debug kits (D01, DXF): no boot challenge
#endif
#ifndef SMC_FIRMWARE_DXF
#define SMC_FIRMWARE_DXF 0 // DXF changes on top of D01
#endif
#ifndef SMC_FIRMWARE_P2L
#define SMC_FIRMWARE_P2L 0 // P2L changes on top of P11
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
#define SMC_AV_PACK_MODE 1 // High Definition AV Pack
#endif

// Front panel LED brightness in percent at start-up
#ifndef SMC_LED_BRIGHTNESS
#define SMC_LED_BRIGHTNESS 100
#endif

// Fade the LEDs at power up and down
#ifndef SMC_LED_FADE
#define SMC_LED_FADE 1
#endif

// Wi-Fi access point with a status and control page, served from core 1
// (W boards only)
#ifndef SMC_WEB_INTERFACE
#define SMC_WEB_INTERFACE 0
#endif
#ifndef SMC_WIFI_SSID
#define SMC_WIFI_SSID "OG-SMC"
#endif
#ifndef SMC_WIFI_PASSWORD
#define SMC_WIFI_PASSWORD "" // Empty: made from the board's unique ID
#endif

#endif // CONFIG_HPP
