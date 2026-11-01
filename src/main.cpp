#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "smc.hpp"
#include "debug.hpp"
#include "web.hpp"

// Set by CMake: the PICO_BOARD this was built for
#ifndef SMC_BOARD
#define SMC_BOARD "unknown"
#endif

int main() {
  stdio_init_all();
  debug::print_welcome();
  // Build options from CMake, see CMakeLists.txt
  debug::print_message(std::string("BUILD: PICO_BOARD=") + SMC_BOARD +
                       " SMC_DVD_SUPPORT=" + (SMC_DVD_SUPPORT ? "ON" : "OFF") +
                       " SMC_BOOT_CHALLENGE=" + (SMC_BOOT_CHALLENGE ? "ON" : "OFF") +
                       " SMC_AV_PACK_OVERRIDE=" + (SMC_AV_PACK_OVERRIDE ? "ON" : "OFF") +
                       " SMC_AV_PACK_MODE=" + std::to_string(SMC_AV_PACK_MODE) +
                       " SMC_LED_BRIGHTNESS=" + std::to_string(SMC_LED_BRIGHTNESS) +
                       " SMC_LED_RED_BREATHE=" + (SMC_LED_RED_BREATHE ? "ON" : "OFF") +
                       " SMC_FIRMWARE=" + SMC_FIRMWARE_NAME +
                       " SMC_REPORT_VERSION=" + SMC::version_string +
                       " SMC_DEBUG_LEVEL=" + debug::level_name() +
                       " SMC_WEB_INTERFACE=" + (SMC_WEB_INTERFACE ? "ON" : "OFF"));
  debug::print_message(std::string("BUILD: Reported version ") + SMC::version_string);
  if (watchdog_caused_reboot()) {
    debug::print_critical("Rebooted by watchdog");
  }
  SMC::main_loop();
}
