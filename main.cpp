#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "i2c_slave/include/i2c_slave.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "smc.hpp"

int main() {
  stdio_init_all();
  printf("SMC Boot\n");
  if (watchdog_caused_reboot()) {
    printf("Warning, rebooted by watchdog!\n");
  }
  SMC::main_loop();
}
