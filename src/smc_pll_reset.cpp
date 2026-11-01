#include "smc_pll_reset.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "pico/stdlib.h"

#include <iterator>
#include <string>

namespace SMC {
namespace PLL_Reset {

flags_struct flags;

pll_sysreset_state state;
uint8_t timeout; // Own counter, 0x49 on the PIC

constexpr const char* state_names[] = {
  "initial",
  "held in reset",
  "start delay (unused)",
  "start PLL",
  "release reset",
  "warm reset",
  "restart PLL off (unused)",
  "restart PLL on (unused)",
  "running"
};

void init() {
  state = pll_sysreset_state::initial;
  timeout = 0;
}

bool isHeldInReset() {
  return (state == pll_sysreset_state::held_in_reset);
}

void printState() {
    const size_t bc_state = static_cast<size_t>(state);
    std::string msg = "PLL Reset state [" + std::to_string(bc_state) + "]";

    if (bc_state < std::size(state_names)) {
        msg += std::string(" ") + state_names[bc_state];
    }

    debug::print_message(msg);
}

// Same order as the PIC (0x601): release SYSRESET, give the encoder ~0.9 ms
// to come out of reset, then configure it. P11 (0x60C) waits ~3 ms and
// configures the temperature sensor instead, leaving the encoder alone.
static void releaseResetAndConfigureEncoder() {
  hal::liftSystemReset();
  flags.system_reset_released = true;
#if SMC_FIRMWARE_P2L
  // P2L (0x94B) only releases SYSRESET, nothing is written
#elif SMC_FIRMWARE_P11
  busy_wait_us(3000);
  configureTemperatureSensor();
#else
  busy_wait_us(900);
  configureConexantEncoder();
#endif
  state = pll_sysreset_state::running;
}

void update() {
  switch (state) {
  case pll_sysreset_state::initial:
    hal::assertSystemReset();
    flags.cold_reset_request = false;
    SMI::flags.os_ready = false;
    Led::flags.led_override = false;
    SMI::flags.reset_on_eject_writable = true;
    SMI::flags.reset_on_eject_disabled = true;
    state = pll_sysreset_state::held_in_reset;
    break;

  case pll_sysreset_state::held_in_reset:
    hal::PLL_off();
    timeout = 0;
    if (flags.cold_reset_request) {
      state = pll_sysreset_state::initial;
    } else if (flags.run_after_reset && hal::get_power_ok()) {
      /* Powered up: enable the PLL, release reset 2 ticks later */
      timeout = 2;
      state = pll_sysreset_state::start_pll;
    }
    break;

  case pll_sysreset_state::start_delay_unused:
    /* Never entered, the PIC has it too (0x5E8) */
    if (!hal::get_power_ok() || flags.cold_reset_request) {
      state = pll_sysreset_state::initial;
    } else if (--timeout == 0) {
      state = pll_sysreset_state::start_pll;
    }
    break;

  case pll_sysreset_state::start_pll:
    hal::PLL_on();
    if (--timeout != 0) {
      return;
    }
    timeout = 1;
    releaseResetAndConfigureEncoder();
    break;

  case pll_sysreset_state::release_reset:
    releaseResetAndConfigureEncoder();
    break;

  case pll_sysreset_state::warm_reset:
    hal::assertSystemReset();
    flags.warm_reset_request = false;
    SMI::flags.os_ready = false;
    SMI::flags.reset_on_eject_writable = true;
    SMI::flags.reset_on_eject_disabled = true;
    Led::flags.led_override = false;
    // Ignore tray changes for a while after a warm reset (P11/P2L, 0x636)
    if (!Dvd::flags.tray_moving) {
      Dvd::restartTrayDebounce();
    }
    state = pll_sysreset_state::release_reset;
    break;

  case pll_sysreset_state::restart_pll_off_unused:
    // Never entered: nothing sets it, also on the PIC (0x62C)
    if (--timeout != 0) {
      return;
    }
    hal::PLL_off();
    hal::assertSystemReset();
    timeout = 1;
    state = pll_sysreset_state::restart_pll_on_unused;
    break;

  case pll_sysreset_state::restart_pll_on_unused:
    if (--timeout != 0) {
      return;
    }
    hal::PLL_on();
    state = pll_sysreset_state::release_reset;
    break;

  case pll_sysreset_state::running:
    /* Wait for power loss or a reset request */
    if (!hal::get_power_ok() || flags.cold_reset_request) {
      state = pll_sysreset_state::initial;
    } else if (flags.warm_reset_request) {
      state = pll_sysreset_state::warm_reset;
    }
    break;

  default:
    state = pll_sysreset_state::initial;
    break;
  }
}

} // namespace PLL_Reset
} // namespace SMC
