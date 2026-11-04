# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialize pico_sdk from installed location
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

# The LED brightness and the Wi-Fi access point name and password are set in
# src/config.hpp. Build folders from before still hold them as cache entries:
# drop those, they're no longer used.
foreach(moved SMC_LED_BRIGHTNESS SMC_WIFI_SSID SMC_WIFI_PASSWORD)
  if(DEFINED CACHE{${moved}})
    message(STATUS "${moved} is now set in src/config.hpp, ignoring '${${moved}}'")
    unset(${moved} CACHE)
  endif()
endforeach()

# Fade the front panel LEDs in at power up and out at power down (~1 s) instead
# of switching them like the PIC: -DSMC_LED_FADE=OFF to switch them
option(SMC_LED_FADE "Fade the LEDs at power up and power down" ON)

# Read an NTC thermistor on GP28 (ADC2), wired from the pin to ground with
# SMC_NTC_SERIES_R from 3.3 V to the pin. Shown in the log and the web
# interface, not on SMBus. -DSMC_NTC_SENSOR=ON [-DSMC_NTC_BETA=3950
# -DSMC_NTC_R25=10000 -DSMC_NTC_SERIES_R=10000]
option(SMC_NTC_SENSOR "Read an NTC thermistor on GP28" OFF)
set(SMC_NTC_BETA 3950 CACHE STRING "NTC beta value (K)")
set(SMC_NTC_R25 10000 CACHE STRING "NTC resistance at 25 C (ohms)")
set(SMC_NTC_SERIES_R 10000 CACHE STRING "Resistor from 3.3 V to the NTC pin (ohms)")
foreach(value SMC_NTC_BETA SMC_NTC_R25 SMC_NTC_SERIES_R)
  if(NOT ${value} MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "${value} must be a positive whole number, got '${${value}}'")
  endif()
endforeach()

# Listen to the whole SMBus (a PIO state machine and a DMA channel) for the
# video encoder: logs which one the CPU finds after power on, and at DEBUG
# level every write it makes to it ("ENCODER:" lines). -DSMC_I2C_SNIFFER=OFF
# to leave it out
option(SMC_I2C_SNIFFER "Log the video encoder found and the CPU's writes to it" ON)

# WS2812 RGB LED on GP27 (Pico pin 32) showing the boot phases: magenta while
# held in reset, blue in the BIOS, cyan after the RAM test, green for 5 s once
# booted, red on a boot failure. -DSMC_RGB_LED=OFF to leave it out
option(SMC_RGB_LED "WS2812 RGB LED on GP27 showing the boot phases" ON)

# Behavior of which original firmware to follow: -DSMC_FIRMWARE=P11, P01, D01,
# DXF or P2L.
# - P11 (1.2/1.3) has lower fan thresholds and configures the temperature
#   sensor instead of the video encoder after reset.
# - D01 (debug kit) is P01 without the boot challenge: only the RAM test is
#   checked.
# - DXF (debug kit) is D01 with an RTC dump instead of a power cycle after boot
#   failures and no I2C bus recovery.
# - P2L (v1.6 Xyclops, 8051) is P11 with its own fan control (CPU temperature
#   only, averaged) and SMBus 0x05/0x0A changes. The temperature still
#   comes from the ADM1032, there's no Xcalibur sensor.
# All of them interlock the power and eject buttons, debounce the AV cable
# removal, handle the DVD tray, power cycle (SMBus 0x22 delay), order the LED
# patterns, check the RAM test result (SMBus 0x12/0x13) and run the boot
# challenge (15-tick RAM test wait, one answer check after a passed RAM test,
# new challenge on every reset, audio muted and no FRAG eject while overheated
# on failure) like P2L.
set(SMC_FIRMWARE P11 CACHE STRING "Original firmware to follow: P11, P01, D01, DXF or P2L")
set_property(CACHE SMC_FIRMWARE PROPERTY STRINGS P11 P01 D01 DXF P2L)
string(TOUPPER "${SMC_FIRMWARE}" SMC_FIRMWARE_SETTING)
if(NOT SMC_FIRMWARE_SETTING MATCHES "^(P11|P01|D01|DXF|P2L)$")
  message(FATAL_ERROR "SMC_FIRMWARE must be P11, P01, D01, DXF or P2L, got '${SMC_FIRMWARE}'")
endif()
set(SMC_FIRMWARE_P11 OFF)  # P11 changes
set(SMC_FIRMWARE_NO_CHALLENGE OFF)  # Debug kits: no boot challenge
set(SMC_FIRMWARE_DXF OFF)  # DXF changes on top of D01
set(SMC_FIRMWARE_P2L OFF)  # P2L changes on top of P11
if(SMC_FIRMWARE_SETTING STREQUAL "P11")
  set(SMC_FIRMWARE_P11 ON)
elseif(SMC_FIRMWARE_SETTING STREQUAL "D01")
  set(SMC_FIRMWARE_NO_CHALLENGE ON)
elseif(SMC_FIRMWARE_SETTING STREQUAL "DXF")
  set(SMC_FIRMWARE_NO_CHALLENGE ON)
  set(SMC_FIRMWARE_DXF ON)
elseif(SMC_FIRMWARE_SETTING STREQUAL "P2L")
  set(SMC_FIRMWARE_P11 ON)
  set(SMC_FIRMWARE_P2L ON)
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

# Version reported on SMBus 0x01, independent of the behavior (SMC_FIRMWARE):
# -DSMC_REPORT_VERSION=AUTO (same as SMC_FIRMWARE), P01, P11, D01, DXF or P2L
set(SMC_REPORT_VERSION AUTO CACHE STRING "Version reported on SMBus 0x01: AUTO (same as SMC_FIRMWARE), P01, P11, D01, DXF or P2L")
set_property(CACHE SMC_REPORT_VERSION PROPERTY STRINGS AUTO P01 P11 D01 DXF P2L)
# Replaces SMC_REPORT_P01: carry an old ON over once, then drop it
if(DEFINED CACHE{SMC_REPORT_P01})
  if(SMC_REPORT_P01 AND SMC_REPORT_VERSION STREQUAL "AUTO")
    set_property(CACHE SMC_REPORT_VERSION PROPERTY VALUE P01)
    set(SMC_REPORT_VERSION P01)
    message(STATUS "SMC_REPORT_P01=ON is now SMC_REPORT_VERSION=P01")
  endif()
  unset(SMC_REPORT_P01 CACHE)
endif()
string(TOUPPER "${SMC_REPORT_VERSION}" SMC_REPORT_VERSION_SETTING)
if(SMC_REPORT_VERSION_SETTING STREQUAL "AUTO")
  set(SMC_VERSION_REPORTED "${SMC_FIRMWARE_SETTING}")
elseif(SMC_REPORT_VERSION_SETTING MATCHES "^(P01|P11|D01|DXF|P2L)$")
  set(SMC_VERSION_REPORTED "${SMC_REPORT_VERSION_SETTING}")
else()
  message(FATAL_ERROR "SMC_REPORT_VERSION must be AUTO, P01, P11, D01, DXF or P2L, got '${SMC_REPORT_VERSION}'")
endif()
message(STATUS "Reported version (SMBus 0x01): ${SMC_VERSION_REPORTED}")

# Wi-Fi access point with a status and control page, W boards only:
# -DSMC_WEB_INTERFACE=AUTO (default: on for boards with Wi-Fi), ON or OFF.
# Connect to the network (name and password in src/config.hpp) and open
# http://192.168.4.1
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
if(SMC_WEB_ENABLED)
  if(NOT PICO_CYW43_SUPPORTED)
    message(FATAL_ERROR "SMC_WEB_INTERFACE needs a board with Wi-Fi (pico_w, pico2_w), PICO_BOARD is '${PICO_BOARD}'")
  endif()
endif()

# Add executable. Default name is the project name, version 0.1
set(SMC_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
# Fan control: P2L has its own, the others follow the PIC
if(SMC_FIRMWARE_P2L)
  set(SMC_FAN_SRC ${SMC_SRC_DIR}/smc_fan_p2l.cpp)
else()
  set(SMC_FAN_SRC ${SMC_SRC_DIR}/smc_fan_pic.cpp)
endif()

add_executable(OG_SMC ${SMC_SRC_DIR}/utils.cpp
                      ${SMC_SRC_DIR}/debug.cpp
                      ${SMC_SRC_DIR}/main.cpp
                      ${SMC_SRC_DIR}/hal.cpp
                      ${SMC_SRC_DIR}/hal_leds.cpp
                      ${SMC_SRC_DIR}/hal_adc.cpp
                      ${SMC_SRC_DIR}/hal_i2c_master.cpp
                      ${SMC_SRC_DIR}/smc.cpp
                      ${SMC_SRC_DIR}/smc_led.cpp
                      ${SMC_SRC_DIR}/smc_boot_challenge.cpp
                      ${SMC_SRC_DIR}/smc_fan.cpp
                      ${SMC_FAN_SRC}
                      ${SMC_SRC_DIR}/smc_smbus.cpp
                      ${SMC_SRC_DIR}/smc_dvd.cpp
                      ${SMC_SRC_DIR}/smc_front_panel_sw.cpp
                      ${SMC_SRC_DIR}/smc_pll_reset.cpp
                      ${SMC_SRC_DIR}/smc_audio_clamp.cpp
                      ${SMC_SRC_DIR}/smc_SMI.cpp
                      ${SMC_SRC_DIR}/smc_power_standby.cpp
                      ${SMC_SRC_DIR}/smc_video.cpp
                      ${SMC_SRC_DIR}/smc_extra_temp.cpp
                      ${SMC_SRC_DIR}/smc_i2c_sniffer.cpp
                      ${SMC_SRC_DIR}/smc_i2c_decoder.cpp
                      ${SMC_SRC_DIR}/smc_encoder_mode.cpp
                      ${SMC_SRC_DIR}/smc_settings.cpp
                      ${SMC_SRC_DIR}/usb_config.cpp
                      ${SMC_SRC_DIR}/smc_rgb_led.cpp)
pico_generate_pio_header(OG_SMC ${SMC_SRC_DIR}/ws2812.pio)

# Core 0 stack: 4 KB (the SDK default is 2 KB), all of SCRATCH_Y.
# Core 1 runs on its own 8 KB stack, see web.cpp.
target_compile_definitions(OG_SMC PRIVATE PICO_STACK_SIZE=0x1000)

target_compile_definitions(OG_SMC PRIVATE SMC_DVD_SUPPORT=$<BOOL:${SMC_DVD_SUPPORT}>
                                          SMC_BOOT_CHALLENGE=$<BOOL:${SMC_BOOT_CHALLENGE}>
                                          SMC_AV_PACK_OVERRIDE=$<BOOL:${SMC_AV_PACK_OVERRIDE}>
                                          SMC_AV_PACK_MODE=${SMC_AV_PACK_MODE}
                                          SMC_LED_FADE=$<BOOL:${SMC_LED_FADE}>
                                          SMC_I2C_SNIFFER=$<BOOL:${SMC_I2C_SNIFFER}>
                                          SMC_RGB_LED=$<BOOL:${SMC_RGB_LED}>
                                          SMC_NTC_SENSOR=$<BOOL:${SMC_NTC_SENSOR}>
                                          SMC_NTC_BETA=${SMC_NTC_BETA}
                                          SMC_NTC_R25=${SMC_NTC_R25}
                                          SMC_NTC_SERIES_R=${SMC_NTC_SERIES_R}
                                          SMC_FIRMWARE_NAME="${SMC_FIRMWARE_SETTING}"
                                          SMC_FIRMWARE_P11=$<BOOL:${SMC_FIRMWARE_P11}>
                                          SMC_FIRMWARE_NO_CHALLENGE=$<BOOL:${SMC_FIRMWARE_NO_CHALLENGE}>
                                          SMC_FIRMWARE_DXF=$<BOOL:${SMC_FIRMWARE_DXF}>
                                          SMC_FIRMWARE_P2L=$<BOOL:${SMC_FIRMWARE_P2L}>
                                          SMC_VERSION_STRING="${SMC_VERSION_REPORTED}"
                                          SMC_DEBUG_LEVEL=${SMC_DEBUG_LEVEL_INDEX}
                                          SMC_WEB_INTERFACE=$<BOOL:${SMC_WEB_ENABLED}>
                                          SMC_BOARD="${PICO_BOARD}")

pico_set_program_name(OG_SMC "OG_SMC")
pico_set_program_version(OG_SMC "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(OG_SMC 1)
pico_enable_stdio_usb(OG_SMC 1) # Only started in USB config mode, see src/usb_config.hpp

# Add the standard library to the build
target_link_libraries(OG_SMC
        pico_stdlib
        hardware_pwm
        hardware_adc
        hardware_pio
        hardware_dma
        hardware_flash
        pico_flash)

# Project headers, and lwipopts.h for the web interface
target_include_directories(OG_SMC PRIVATE
        ${SMC_SRC_DIR}
)

# Add any user requested libraries
target_link_libraries(OG_SMC
        hardware_i2c
        hardware_watchdog
        pico_i2c_slave
        )

if(SMC_WEB_ENABLED)
  target_sources(OG_SMC PRIVATE ${SMC_SRC_DIR}/web.cpp ${SMC_SRC_DIR}/dhcp_server.cpp)
  target_link_libraries(OG_SMC pico_cyw43_arch_lwip_poll pico_multicore pico_unique_id)
endif()

# Extra warnings for our own sources only: the SDK, lwIP and TinyUSB build in
# the same target and aren't written for them
get_target_property(SMC_TARGET_SOURCES OG_SMC SOURCES)
list(FILTER SMC_TARGET_SOURCES INCLUDE REGEX "^${SMC_SRC_DIR}/")
set_source_files_properties(${SMC_TARGET_SOURCES} PROPERTIES
                            COMPILE_OPTIONS "-Wall;-Wextra;-Wshadow;-Wconversion")

pico_add_extra_outputs(OG_SMC)
