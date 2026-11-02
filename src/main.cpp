#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "smc.hpp"
#include "debug.hpp"
#include "web.hpp"
#include "usb_config.hpp"

int main() {
  // UART only: USB is only started by UsbConfig::detect(), out of the Xbox
  stdio_uart_init();
  debug::print_welcome();
  debug::print_build_flags();

  if (watchdog_caused_reboot()) {
    debug::print_critical("Rebooted by watchdog");
  }

  // Out of the Xbox, powered from USB: configuration instead of the SMC
  if (UsbConfig::detect()) {
    UsbConfig::run();
  }

  SMC::main_loop();
}
