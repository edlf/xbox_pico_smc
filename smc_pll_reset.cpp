#include "smc_pll_reset.hpp"
#include "smc.hpp"
#include "smc_fan.hpp"
#include "hal.hpp"
#include "utils.hpp"
#include "pico/stdlib.h" // TODO move busywait to hal

namespace SMC {
namespace PLL_Reset {

pll_sysreset_state state;

void init() {
  state = pll_sysreset_state::initial;
}

bool isState1() {
  return (state == pll_sysreset_state::state1);
}

void setWarmReset1() {
  state = pll_sysreset_state::warm_reset_1;
}

void setColdReset() {
  state = pll_sysreset_state::cold_reset;
}

void update() {
  switch (state) {
  case pll_sysreset_state::initial:
    hal::assertSystemReset();
    flags.bitfield_DATA_70 &= ~0x01; // Clear flags
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_72 |= 0x04; // Set PLL control flags
    flags.bitfield_DATA_72 |= 0x02;
    state = pll_sysreset_state::state1;
    break;

  case pll_sysreset_state::state1:
    hal::PLL_off();
    Fan::control_timeout = 0;

    if (flags.bitfield_DATA_70 & 0x01) {
      /* Cold reset path - go back to state 0 */
      state = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x02) {
      /* Check PORTC.1 (POWOK signal) */
      if (hal::get_power_ok()) {
        /* POWOK is high */
        state = pll_sysreset_state::cold_reset;
      }
    } else {
      /* Normal transition to wait state */
      return;
    }
    break;

  case pll_sysreset_state::state2:
    /* Wait state - checking for timing conditions */
    if (!hal::get_power_ok()) {
      /* POWOK low - return to state 0 */
      state = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* Reset condition detected */
      state = pll_sysreset_state::initial;
    } else {
      /* Decrement timer and check for completion */
      if (Fan::control_timeout == 0) {
        Fan::control_timeout = 2;
        state = pll_sysreset_state::cold_reset;
      }
    }
    break;

  case pll_sysreset_state::cold_reset:
    hal::PLL_on();

    if (--Fan::control_timeout != 0) {
      return; // Wait for timeout
    }

    Fan::control_timeout = 1;
    configureConexantEncoder();
    hal::liftSystemReset();
    flags.bitfield_DATA_73 |= 0x08; // Set initialization flag

    /* Wait for encoder communication */
    busy_wait_ms(132);

    // TODO: Send SMBus write command

    state = pll_sysreset_state::state8;
    break;

  case pll_sysreset_state::warm_reset_2:
    hal::liftSystemReset();
    configureConexantEncoder();
    flags.bitfield_DATA_73 |= 0x08; // Set initialization flag

    /* Wait for encoder communication */
    busy_wait_ms(132); // Delay ~832 cycles

    state = pll_sysreset_state::state8;
    break;

  case pll_sysreset_state::warm_reset_1:
    hal::assertSystemReset();
    flags.bitfield_DATA_73 &= ~0x01; // Clear reset request
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_72 |= 0x04; // Maintain PLL control state
    flags.bitfield_DATA_72 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    state = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state6:
    /* PLL disable wait - decrement counter */
    if (--Fan::control_timeout != 0) {
      return;
    }

    hal::PLL_off();
    hal::assertSystemReset();
    Fan::control_timeout = 1;
    state = pll_sysreset_state::state7;
    break;

  case pll_sysreset_state::state7:
    /* PLL enable wait - wait before re-enabling PLL */
    if (--Fan::control_timeout != 0) {
      return;
    }

    hal::PLL_on();
    state = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state8:
    /* Final state - wait for completion or reset request */
    if (!hal::get_power_ok()) {
      state = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* Something needs resetting */
      state = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_73 & 0x01) {
      /* System reset requested */
      state = pll_sysreset_state::warm_reset_1; // Go to warm reset path
    }
    break;

  default:
    state = pll_sysreset_state::initial;
    break;
  }
}

} // namespace PLL_Reset
} // namespace SMC
