#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"

namespace SMC {

void update_pwr_sw() {
  // Done, untested
  switch (state.pwr_sw) {
  case power_switch_state::wait_for_button_press:
    state.smi_power = smi_power_state::initial;
    if (pico_hal::power_button_pressed()) {
      state.pwr_sw = power_switch_state::debouncing;
    }
    break;

  case power_switch_state::debouncing:
    // state.smi_power = smi_power_state::overheat_cooldown_wait;
    if (pico_hal::power_button_pressed()) {
      setStatusBit(power_change_requested);
      state.pwr_sw = power_switch_state::wait_for_button_release;
    } else {
      state.pwr_sw = power_switch_state::wait_for_button_press;
    }
    break;

  case power_switch_state::wait_for_button_release:
    state.smi_power = smi_power_state::case11;
    if (!pico_hal::power_button_pressed()) {
      state.pwr_sw = power_switch_state::release_debounce;
    }
    break;

  case power_switch_state::release_debounce:
    state.smi_power = smi_power_state::going_to_reset;
    if (pico_hal::power_button_pressed()) {
      state.pwr_sw = power_switch_state::wait_for_button_release;
    } else {
      state.pwr_sw = power_switch_state::wait_for_button_press;
    }
    break;
  }
}

void update_eject_sw() {
  // Done, untested
  switch (state.eject_sw) {
  case eject_switch_state::wait_for_button_press:
    if (pico_hal::eject_button_pressed()) {
      state.eject_sw = eject_switch_state::debouncing;
    }
    break;

  case eject_switch_state::debouncing:
    if (pico_hal::eject_button_pressed()) {
      setStatusBit(power_change_requested);
      state.eject_sw = eject_switch_state::wait_for_button_release;
    } else {
      state.eject_sw = eject_switch_state::wait_for_button_press;
    }
    break;

  case eject_switch_state::wait_for_button_release:
    if (!pico_hal::eject_button_pressed()) {
      state.eject_sw = eject_switch_state::release_debounce;
    }
    break;

  case eject_switch_state::release_debounce:
    if (pico_hal::eject_button_pressed()) {
      state.eject_sw = eject_switch_state::wait_for_button_release;
    } else {
      state.eject_sw = eject_switch_state::wait_for_button_press;
    }
    break;
  }
}

} // namespace SMC
