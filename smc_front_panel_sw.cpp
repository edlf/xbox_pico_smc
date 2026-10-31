#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"

#include "time.h"

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

switch_state pwr_state = switch_state::wait_for_button_press;
switch_state eject_state = switch_state::wait_for_button_press;

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
    if (hal::power_button_pressed()) {
      pwr_state = switch_state::debouncing;
    }
    break;

  case switch_state::debouncing:
    if (hal::power_button_pressed()) {
      flags.power_change_requested = true;
      pwr_state = switch_state::wait_for_button_release;
    } else {
      pwr_state = switch_state::wait_for_button_press;
    }
    break;

  case switch_state::wait_for_button_release:
    if (!hal::power_button_pressed()) {
      pwr_state = switch_state::release_debounce;
    }
    break;

  case switch_state::release_debounce:
    if (hal::power_button_pressed()) {
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
    if (hal::eject_button_pressed()) {
      eject_state = switch_state::debouncing;
    }
    break;

  case switch_state::debouncing:
    if (hal::eject_button_pressed()) {
      flags.eject_change_requested = true;
      flags.eject_button_event = true;
      eject_state = switch_state::wait_for_button_release;
    } else {
      eject_state = switch_state::wait_for_button_press;
    }
    break;

  case switch_state::wait_for_button_release:
    if (!hal::eject_button_pressed()) {
      eject_state = switch_state::release_debounce;
    }
    break;

  case switch_state::release_debounce:
    if (hal::eject_button_pressed()) {
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
