#include "smc_pll_reset.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"
#include "pico/stdlib.h"

#include <vector>
#include <string>

namespace SMC {
namespace PLL_Reset {

pll_sysreset_state state;
uint8_t timeout; // Own counter, 0x49 on the PIC

const std::vector<std::string> state_names {
  "initial",
  "state1",
  "state2",
  "cold_reset",
  "warm_reset_2",
  "warm_reset_1",
  "state6",
  "state7",
  "state8"
};

void init() {
  state = pll_sysreset_state::initial;
  timeout = 0;
}

bool isState1() {
  return (state == pll_sysreset_state::state1);
}

void printState() {
    const size_t bc_state = static_cast<size_t>(state);
    std::string msg = "PLL Reset state [" + std::to_string(bc_state) + "]";

    if (bc_state <= state_names.size()) {
        msg += " " + state_names[bc_state];
    }

    debug::print_message(msg);
}

// Same order as the PIC (0x601): release SYSRESET, give the encoder ~0.9 ms
// to come out of reset, then configure it. P11 (0x60C) waits ~3 ms and
// configures the temperature sensor instead, leaving the encoder alone.
static void releaseResetAndConfigureEncoder() {
  hal::liftSystemReset();
  flags.bitfield_DATA_73 |= 0x08;
#if SMC_FIRMWARE_P11
  busy_wait_us(3000);
  configureTemperatureSensor();
#else
  busy_wait_us(900);
  configureEncoder();
#endif
  state = pll_sysreset_state::state8;
}

void update() {
  switch (state) {
  case pll_sysreset_state::initial:
    hal::assertSystemReset();
    flags.bitfield_DATA_70 &= ~0x01;
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_72 |= 0x04;
    flags.bitfield_DATA_72 |= 0x02;
    state = pll_sysreset_state::state1;
    break;

  case pll_sysreset_state::state1:
    hal::PLL_off();
    timeout = 0;
    if (flags.bitfield_DATA_70 & 0x01) {
      state = pll_sysreset_state::initial;
    } else if ((flags.bitfield_DATA_70 & 0x02) && hal::get_power_ok()) {
      /* Powered up: enable the PLL, release reset 2 ticks later */
      timeout = 2;
      state = pll_sysreset_state::cold_reset;
    }
    break;

  case pll_sysreset_state::state2:
    /* Not reachable on the PIC either */
    if (!hal::get_power_ok() || (flags.bitfield_DATA_70 & 0x01)) {
      state = pll_sysreset_state::initial;
    } else if (--timeout == 0) {
      state = pll_sysreset_state::cold_reset;
    }
    break;

  case pll_sysreset_state::cold_reset:
    hal::PLL_on();
    if (--timeout != 0) {
      return;
    }
    timeout = 1;
    releaseResetAndConfigureEncoder();
    break;

  case pll_sysreset_state::warm_reset_2:
    releaseResetAndConfigureEncoder();
    break;

  case pll_sysreset_state::warm_reset_1:
    hal::assertSystemReset();
    flags.bitfield_DATA_73 &= ~0x01; // Clear reset request
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_72 |= 0x04;
    flags.bitfield_DATA_72 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
#if SMC_FIRMWARE_P11
    if (!utils::checkBitNo(flags.bitfield_DATA_6F, 5)) { // Tray not moving
      Dvd::restartTrayDebounce();
    }
#endif
    state = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state6:
    if (--timeout != 0) {
      return;
    }
    hal::PLL_off();
    hal::assertSystemReset();
    timeout = 1;
    state = pll_sysreset_state::state7;
    break;

  case pll_sysreset_state::state7:
    if (--timeout != 0) {
      return;
    }
    hal::PLL_on();
    state = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state8:
    /* Running - wait for power loss or a reset request */
    if (!hal::get_power_ok() || (flags.bitfield_DATA_70 & 0x01)) {
      state = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_73 & 0x01) {
      state = pll_sysreset_state::warm_reset_1;
    }
    break;

  default:
    state = pll_sysreset_state::initial;
    break;
  }
}

} // namespace PLL_Reset
} // namespace SMC
