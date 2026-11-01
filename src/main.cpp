#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "smc.hpp"
#include "debug.hpp"
#include "web.hpp"

int main() {
  stdio_init_all();
  debug::print_welcome();
  debug::print_build_flags();

  if (watchdog_caused_reboot()) {
    debug::print_critical("Rebooted by watchdog");
  }

  SMC::main_loop();
}
