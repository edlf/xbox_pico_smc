#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"
#include "pico/stdlib.h" // TODO move busywait to pico_hal

namespace SMC {

void update_PLL_SYSRESET() {
  switch (state.pll_reset) {
  case pll_sysreset_state::initial:
    pico_hal::assertSystemReset();
    flags.bitfield_DATA_70 &= ~0x01; // Clear flags
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_72 |= 0x04; // Set PLL control flags
    flags.bitfield_DATA_72 |= 0x02;
    state.pll_reset = pll_sysreset_state::state1;
    break;

  case pll_sysreset_state::state1:
    pico_hal::PLL_off();
    timers.fan_control_timeout1 = 0;

    if (flags.bitfield_DATA_70 & 0x01) {
      /* Cold reset path - go back to state 0 */
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x02) {
      /* Check PORTC.1 (POWOK signal) */
      if (pico_hal::get_power_ok()) {
        /* POWOK is high */
        state.pll_reset = pll_sysreset_state::cold_reset;
      }
    } else {
      /* Normal transition to wait state */
      return;
    }
    break;

  case pll_sysreset_state::state2:
    /* Wait state - checking for timing conditions */
    if (!pico_hal::get_power_ok()) {
      /* POWOK low - return to state 0 */
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* Reset condition detected */
      state.pll_reset = pll_sysreset_state::initial;
    } else {
      /* Decrement timer and check for completion */
      if (timers.fan_control_timeout1 == 0) {
        timers.fan_control_timeout1 = 2;
        state.pll_reset = pll_sysreset_state::cold_reset;
      }
    }
    break;

  case pll_sysreset_state::cold_reset:
    pico_hal::PLL_on();

    if (--timers.fan_control_timeout1 != 0) {
      return; // Wait for timeout
    }

    timers.fan_control_timeout1 = 1;
    configureConexantEncoder();
    pico_hal::liftSystemReset();
    flags.bitfield_DATA_73 |= 0x08; // Set initialization flag

    /* Wait for encoder communication */
    busy_wait_ms(132);

    // TODO: Send SMBus write command

    state.pll_reset = pll_sysreset_state::state8;
    break;

  case pll_sysreset_state::warm_reset_2:
    pico_hal::liftSystemReset();
    configureConexantEncoder();
    flags.bitfield_DATA_73 |= 0x08; // Set initialization flag

    /* Wait for encoder communication */
    busy_wait_ms(132); // Delay ~832 cycles

    state.pll_reset = pll_sysreset_state::state8;
    break;

  case pll_sysreset_state::warm_reset_1:
    pico_hal::assertSystemReset();
    flags.bitfield_DATA_73 &= ~0x01; // Clear reset request
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_72 |= 0x04; // Maintain PLL control state
    flags.bitfield_DATA_72 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    state.pll_reset = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state6:
    /* PLL disable wait - decrement counter */
    if (--timers.fan_control_timeout1 != 0) {
      return;
    }

    pico_hal::PLL_off();
    pico_hal::assertSystemReset();
    timers.fan_control_timeout1 = 1;
    state.pll_reset = pll_sysreset_state::state7;
    break;

  case pll_sysreset_state::state7:
    /* PLL enable wait - wait before re-enabling PLL */
    if (--timers.fan_control_timeout1 != 0) {
      return;
    }

    pico_hal::PLL_on();
    state.pll_reset = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state8:
    /* Final state - wait for completion or reset request */
    if (!pico_hal::get_power_ok()) {
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* Something needs resetting */
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_73 & 0x01) {
      /* System reset requested */
      state.pll_reset = pll_sysreset_state::warm_reset_1; // Go to warm reset path
    }
    break;

  default:
    state.pll_reset = pll_sysreset_state::initial;
    break;
  }
}

} // namespace SMC
