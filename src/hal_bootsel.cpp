// Work that needs the flash untouched (the other core paused, interrupts off),
// and the BOOTSEL button, which is read through the flash chip's select line

#include "hal.hpp"

#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#if LIB_PICO_MULTICORE
#include "pico/multicore.h"
#endif

namespace hal {

int run_flash_safe(void (*func)(void*), void* param, [[maybe_unused]] const uint32_t timeout_ms) {
#if LIB_PICO_MULTICORE
  // Core 1 (the web interface) running: flash_safe_execute() pauses it
  if (multicore_lockout_victim_is_initialized(1)) {
    return flash_safe_execute(func, param, timeout_ms);
  }
#endif
  // Only this core runs: with its interrupts off nothing else reads the flash.
  // (flash_safe_execute() refuses this case in builds with pico_multicore.)
  const uint32_t saved = save_and_disable_interrupts();
  func(param);
  restore_interrupts(saved);
  return PICO_OK;
}

// From RAM, with nothing reading the flash (run_flash_safe()): the BOOTSEL
// button pulls the flash's chip select low. Its output is turned off for a
// moment so the line can be read, as the SDK's picoboard/button example.
static void __no_inline_not_in_flash_func(readBootsel)(void* pressed) {
  constexpr uint cs_pin_index = 1;
  hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl, GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
  for (volatile int i = 0; i < 1000; i++) {
    // Let the line settle
  }
#if PICO_RP2040
  constexpr uint32_t cs_bit = 1u << 1;
#else
  constexpr uint32_t cs_bit = SIO_GPIO_HI_IN_QSPI_CSN_BITS;
#endif
  *static_cast<bool*>(pressed) = (sio_hw->gpio_hi_in & cs_bit) == 0;
  hw_write_masked(&ioqspi_hw->io[cs_pin_index].ctrl, GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB, IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
}

bool bootsel_button_pressed() {
  bool pressed = false;
  if (run_flash_safe(readBootsel, &pressed, 5) != PICO_OK) {
    return false;  // Core 1 didn't pause in time: read it next tick
  }
  return pressed;
}

}  // namespace hal
