#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "smc.hpp"
#include "debug.hpp"

int main() {
  stdio_init_all();
  debug::print_welcome();
  // Build options from CMake, see CMakeLists.txt
  debug::print_message(std::string("BUILD: SMC_DVD_SUPPORT=") + (SMC_DVD_SUPPORT ? "ON" : "OFF") +
                       " SMC_BOOT_CHALLENGE=" + (SMC_BOOT_CHALLENGE ? "ON" : "OFF") +
                       " SMC_AV_PACK_OVERRIDE=" + (SMC_AV_PACK_OVERRIDE ? "ON" : "OFF") +
                       " SMC_AV_PACK_MODE=" + std::to_string(SMC_AV_PACK_MODE) +
                       " SMC_LED_BRIGHTNESS=" + std::to_string(SMC_LED_BRIGHTNESS) +
                       " SMC_LED_RED_BREATHE=" + (SMC_LED_RED_BREATHE ? "ON" : "OFF"));
  if (watchdog_caused_reboot()) {
    debug::print_critical("Rebooted by watchdog");
  }
  SMC::main_loop();
}
