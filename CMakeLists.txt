# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Supported boards: pico, pico_w, pico2, pico2_w. Pick one with -DPICO_BOARD=...
# in a fresh build directory (pico2/pico2_w build for the RP2350). On the W
# boards the onboard LED is on the wireless chip and isn't used.
set(SMC_SUPPORTED_BOARDS pico pico_w pico2 pico2_w)
if(NOT PICO_BOARD IN_LIST SMC_SUPPORTED_BOARDS)
  message(WARNING "PICO_BOARD '${PICO_BOARD}' is untested, supported: ${SMC_SUPPORTED_BOARDS}")
endif()

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(OG_SMC C CXX ASM)

# Initialize the Raspberry Pi Pico SDK
pico_sdk_init()

# add_compile_definitions(SYS_CLK_MHZ=200)

# Build without DVD drive support for drive-less systems: -DSMC_DVD_SUPPORT=OFF
option(SMC_DVD_SUPPORT "Support a DVD drive (tray, eject)" ON)

# Enforce the boot challenge like the original: reset, power cycle and FRAG
# when the RAM test or the challenge fails. -DSMC_BOOT_CHALLENGE=ON
option(SMC_BOOT_CHALLENGE "Reset the Xbox when the boot challenge fails" OFF)

# Report a fixed AV pack instead of reading the AV connector:
# -DSMC_AV_PACK_OVERRIDE=ON [-DSMC_AV_PACK_MODE=n]. Codes (SMBus 0x04): 0 SCART,
# 1 High Definition AV Pack, 2 VGA, 3 RF, 4 Advanced AV (S-Video), 6 Standard AV
option(SMC_AV_PACK_OVERRIDE "Report SMC_AV_PACK_MODE instead of the detected AV pack" OFF)
set(SMC_AV_PACK_MODE 1 CACHE STRING "AV pack code reported when overridden (1 = High Definition AV Pack)")
set_property(CACHE SMC_AV_PACK_MODE PROPERTY STRINGS 0 1 2 3 4 5 6)
if(NOT SMC_AV_PACK_MODE MATCHES "^[0-6]$")
  message(FATAL_ERROR "SMC_AV_PACK_MODE must be 0-6, got '${SMC_AV_PACK_MODE}'")
endif()

# Front panel LED brightness in percent (the PIC only has on/off):
# -DSMC_LED_BRIGHTNESS=n
set(SMC_LED_BRIGHTNESS 100 CACHE STRING "LED brightness in percent (1-100)")
if(NOT SMC_LED_BRIGHTNESS MATCHES "^[0-9]+$" OR SMC_LED_BRIGHTNESS LESS 1 OR SMC_LED_BRIGHTNESS GREATER 100)
  message(FATAL_ERROR "SMC_LED_BRIGHTNESS must be 1-100, got '${SMC_LED_BRIGHTNESS}'")
endif()

# Red LED breathes (1 s pulse) while on instead of staying solid:
# -DSMC_LED_RED_BREATHE=ON
option(SMC_LED_RED_BREATHE "Red LED breathes while on" OFF)

# Behaviour of which original PIC firmware to follow: -DSMC_FIRMWARE=P11 or P01.
# P11 (1.2/1.3) has lower fan thresholds, SMBus 0x22 (power cycle delay), a
# shorter power cycle, LED overrides over the AV/off patterns and configures
# the temperature sensor instead of the video encoder after reset.
set(SMC_FIRMWARE P11 CACHE STRING "Original firmware to follow: P11 or P01")
set_property(CACHE SMC_FIRMWARE PROPERTY STRINGS P11 P01)
string(TOUPPER "${SMC_FIRMWARE}" SMC_FIRMWARE_SETTING)
if(SMC_FIRMWARE_SETTING STREQUAL "P11")
  set(SMC_FIRMWARE_P11 ON)
elseif(SMC_FIRMWARE_SETTING STREQUAL "P01")
  set(SMC_FIRMWARE_P11 OFF)
else()
  message(FATAL_ERROR "SMC_FIRMWARE must be P11 or P01, got '${SMC_FIRMWARE}'")
endif()

# Log verbosity on the USB/UART console: -DSMC_DEBUG_LEVEL=DEBUG adds the SMBus
# transactions. SILENT, CRITICAL, WARNINGS, INFO or DEBUG.
set(SMC_DEBUG_LEVEL INFO CACHE STRING "Log level: SILENT, CRITICAL, WARNINGS, INFO or DEBUG")
set(SMC_DEBUG_LEVELS SILENT CRITICAL WARNINGS INFO DEBUG)
set_property(CACHE SMC_DEBUG_LEVEL PROPERTY STRINGS ${SMC_DEBUG_LEVELS})
string(TOUPPER "${SMC_DEBUG_LEVEL}" SMC_DEBUG_LEVEL_SETTING)
list(FIND SMC_DEBUG_LEVELS "${SMC_DEBUG_LEVEL_SETTING}" SMC_DEBUG_LEVEL_INDEX)
if(SMC_DEBUG_LEVEL_INDEX LESS 0)
  message(FATAL_ERROR "SMC_DEBUG_LEVEL must be SILENT, CRITICAL, WARNINGS, INFO or DEBUG, got '${SMC_DEBUG_LEVEL}'")
endif()

# Report "P01" on SMBus 0x01 while following P11: -DSMC_REPORT_P01=ON
option(SMC_REPORT_P01 "Report version P01 on SMBus 0x01, whatever SMC_FIRMWARE is" OFF)

# Wi-Fi access point with a status and control page, W boards only:
# -DSMC_WEB_INTERFACE=AUTO (default: on for boards with Wi-Fi), ON or OFF.
# [-DSMC_WIFI_SSID=... -DSMC_WIFI_PASSWORD=...] Connect to the network and
# open http://192.168.4.1
set(SMC_WEB_INTERFACE AUTO CACHE STRING "Wi-Fi access point with a status and control page: AUTO (on for boards with Wi-Fi), ON or OFF")
set_property(CACHE SMC_WEB_INTERFACE PROPERTY STRINGS AUTO ON OFF)
# Build folders from before AUTO existed hold the old plain ON/OFF default
get_property(SMC_WEB_INTERFACE_TYPE CACHE SMC_WEB_INTERFACE PROPERTY TYPE)
if(SMC_WEB_INTERFACE_TYPE STREQUAL "BOOL")
  set_property(CACHE SMC_WEB_INTERFACE PROPERTY TYPE STRING)
  set_property(CACHE SMC_WEB_INTERFACE PROPERTY VALUE AUTO)
endif()
string(TOUPPER "${SMC_WEB_INTERFACE}" SMC_WEB_INTERFACE_SETTING)
if(SMC_WEB_INTERFACE_SETTING STREQUAL "AUTO")
  if(PICO_CYW43_SUPPORTED)
    set(SMC_WEB_ENABLED ON)
  else()
    set(SMC_WEB_ENABLED OFF)
  endif()
elseif(SMC_WEB_INTERFACE_SETTING MATCHES "^(ON|YES|TRUE|1)$")
  set(SMC_WEB_ENABLED ON)
elseif(SMC_WEB_INTERFACE_SETTING MATCHES "^(OFF|NO|FALSE|0)$")
  set(SMC_WEB_ENABLED OFF)
else()
  message(FATAL_ERROR "SMC_WEB_INTERFACE must be AUTO, ON or OFF, got '${SMC_WEB_INTERFACE}'")
endif()
message(STATUS "Web interface (SMC_WEB_INTERFACE=${SMC_WEB_INTERFACE}): ${SMC_WEB_ENABLED}")
set(SMC_WIFI_SSID "OG-SMC" CACHE STRING "Access point name")
set(SMC_WIFI_PASSWORD "" CACHE STRING "Access point WPA2 password (8-63 characters), empty: made from the board's unique ID and shown in the boot log")
# Build folders from before the per-board password still hold the old fixed
# default, which nobody chose: drop it
if(SMC_WIFI_PASSWORD STREQUAL "og-smc-xbox")
  set_property(CACHE SMC_WIFI_PASSWORD PROPERTY VALUE "")
  set(SMC_WIFI_PASSWORD "")
  message(STATUS "SMC_WIFI_PASSWORD was the old default, now made from the board ID")
endif()
if(SMC_WEB_ENABLED)
  if(NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR "SMC_WEB_INTERFACE needs a board with Wi-Fi (pico_w, pico2_w), PICO_BOARD is '${PICO_BOARD}'")
  endif()
  string(LENGTH "${SMC_WIFI_PASSWORD}" SMC_WIFI_PASSWORD_LENGTH)
  if(SMC_WIFI_PASSWORD_LENGTH GREATER 0 AND (SMC_WIFI_PASSWORD_LENGTH LESS 8 OR SMC_WIFI_PASSWORD_LENGTH GREATER 63))
    message(FATAL_ERROR "SMC_WIFI_PASSWORD must be 8-63 characters, or empty for one made from the board's unique ID")
  endif()
endif()

# Add executable. Default name is the project name, version 0.1
add_executable(OG_SMC utils.cpp
                      debug.cpp
                      main.cpp
                      hal.cpp
                      smc.cpp
                      smc_led.cpp
                      smc_boot_challenge.cpp
                      smc_fan.cpp
                      smc_smbus.cpp
                      smc_dvd.cpp
                      smc_front_panel_sw.cpp
                      smc_pll_reset.cpp
                      smc_audio_clamp.cpp
                      smc_SMI.cpp
                      smc_power_standby.cpp
                      smc_video.cpp)

target_compile_definitions(OG_SMC PRIVATE SMC_DVD_SUPPORT=$<BOOL:${SMC_DVD_SUPPORT}>
                                          SMC_BOOT_CHALLENGE=$<BOOL:${SMC_BOOT_CHALLENGE}>
                                          SMC_AV_PACK_OVERRIDE=$<BOOL:${SMC_AV_PACK_OVERRIDE}>
                                          SMC_AV_PACK_MODE=${SMC_AV_PACK_MODE}
                                          SMC_LED_BRIGHTNESS=${SMC_LED_BRIGHTNESS}
                                          SMC_LED_RED_BREATHE=$<BOOL:${SMC_LED_RED_BREATHE}>
                                          SMC_FIRMWARE_P11=$<BOOL:${SMC_FIRMWARE_P11}>
                                          SMC_REPORT_P01=$<BOOL:${SMC_REPORT_P01}>
                                          SMC_DEBUG_LEVEL=${SMC_DEBUG_LEVEL_INDEX}
                                          SMC_WEB_INTERFACE=$<BOOL:${SMC_WEB_ENABLED}>
                                          SMC_BOARD="${PICO_BOARD}")

pico_set_program_name(OG_SMC "OG_SMC")
pico_set_program_version(OG_SMC "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(OG_SMC 1)
pico_enable_stdio_usb(OG_SMC 0)

# Add the standard library to the build
target_link_libraries(OG_SMC
        pico_stdlib
        hardware_pwm)

# Add the standard include files to the build
target_include_directories(OG_SMC PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(OG_SMC
        hardware_i2c
        hardware_watchdog
        pico_i2c_slave
        )

if(SMC_WEB_ENABLED)
  target_sources(OG_SMC PRIVATE web.cpp dhcp_server.cpp)
  target_compile_definitions(OG_SMC PRIVATE SMC_WIFI_SSID="${SMC_WIFI_SSID}"
                                            SMC_WIFI_PASSWORD="${SMC_WIFI_PASSWORD}")
  target_link_libraries(OG_SMC pico_cyw43_arch_lwip_poll pico_multicore pico_unique_id)
endif()

pico_add_extra_outputs(OG_SMC)
