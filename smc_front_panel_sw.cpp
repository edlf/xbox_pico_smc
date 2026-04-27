#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"
#include "smc_front_panel_sw.hpp"

#include <vector>
#include <string>

namespace SMC {
namespace FrontPanelSW {

enum class switch_state {
  wait_for_button_press,
  debouncing,
  wait_for_button_release,
  release_debounce
};

const std::vector<std::string> state_names {
  "Wait for button press",
  "Debouncing",
  "Wait for button release",
  "Release debounce"
};

switch_state pwr_state;
switch_state eject_state;

void init() {
  pwr_state = switch_state::wait_for_button_press;
  eject_state = switch_state::wait_for_button_press;
}

void update() {
  updatePower();
  updateEject();
}

void updatePower() {
  switch (pwr_state) {
  case switch_state::wait_for_button_press:
    state.smi_power = smi_power_state::initial;
    if (pico_hal::power_button_pressed()) {
      pwr_state = switch_state::debouncing;
    }
    break;

  case switch_state::debouncing:
    // TODO: state.smi_power = smi_power_state::overheat_cooldown_wait;
    if (pico_hal::power_button_pressed()) {
      setStatusBit(power_change_requested);
      pwr_state = switch_state::wait_for_button_release;
    } else {
      pwr_state = switch_state::wait_for_button_press;
    }
    break;

  case switch_state::wait_for_button_release:
    state.smi_power = smi_power_state::case11;
    if (!pico_hal::power_button_pressed()) {
      pwr_state = switch_state::release_debounce;
    }
    break;

  case switch_state::release_debounce:
    state.smi_power = smi_power_state::going_to_reset;
    if (pico_hal::power_button_pressed()) {
      pwr_state = switch_state::wait_for_button_release;
    } else {
      pwr_state = switch_state::wait_for_button_press;
    }
    break;
  }
}

void updateEject() {
  switch (eject_state) {
  case switch_state::wait_for_button_press:
    if (pico_hal::eject_button_pressed()) {
      eject_state = switch_state::debouncing;
    }
    break;

  case switch_state::debouncing:
    if (pico_hal::eject_button_pressed()) {
      setStatusBit(power_change_requested);
      eject_state = switch_state::wait_for_button_release;
    } else {
      eject_state = switch_state::wait_for_button_press;
    }
    break;

  case switch_state::wait_for_button_release:
    if (!pico_hal::eject_button_pressed()) {
      eject_state = switch_state::release_debounce;
    }
    break;

  case switch_state::release_debounce:
    if (pico_hal::eject_button_pressed()) {
      eject_state = switch_state::wait_for_button_release;
    } else {
      eject_state = switch_state::wait_for_button_press;
    }
    break;
  }
}

void printPowerState() {
    const size_t p_state = static_cast<size_t>(pwr_state);
    std::string msg = "Power SW state [" + std::to_string(p_state) + "]";

    if (p_state <= state_names.size()) {
        msg += " " + state_names[p_state];
    }

    debug::print_message(msg);
}
void printEjectState() {
    const size_t e_state = static_cast<size_t>(eject_state);
    std::string msg = "Eject SW state [" + std::to_string(e_state) + "]";

    if (e_state <= state_names.size()) {
        msg += " " + state_names[e_state];
    }

    debug::print_message(msg);
}

} // namespace FrontPanelSW
} // namespace SMC
