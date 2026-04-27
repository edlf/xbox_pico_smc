#include "fsm_led.hpp"
#include "smc_types.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"
#include "debug.hpp"

#include <vector>
#include <string>

namespace SMC {

const std::vector<std::string> led_state_names {
  "Initial",
  "Overheat",
  "Manual control",
  "Quick green blink",
  "Solid green",
  "Off",
  "Tick update",
  "Set GPIOs",
  "Reset phase counter",
  "Quick green/orange (Missing AV)"
};

FSM_Leds::FSM_Leds() {
  state = led_state::initial;
  state_counter = 0;
  green_phases_manual = 0;
  red_phases_manual = 0;
  green_phases = 0;
  red_phases = 0;
  manual_control = false;
}

FSM_Leds::~FSM_Leds() {
}

void FSM_Leds::reset_phase_counter() {
    state = led_state::reset_phase_counter;
}

void FSM_Leds::reset_state() {
    state = led_state::initial;
    manual_control = false;
}

void FSM_Leds::setGreenPhases(const uint8_t phase) {
    green_phases = phase;
}

void FSM_Leds::setRedPhases(const uint8_t phase) {
    red_phases = phase;
}

void FSM_Leds::setManualControl(const bool mc) {
    manual_control = mc;
}

void FSM_Leds::printState() {
    const size_t led_state = static_cast<size_t>(state);
    std::string msg = "LED state [" + std::to_string(led_state) + "]";

    if (led_state <= led_state_names.size()) {
        msg += " " + led_state_names[led_state];
    }

    debug::print_message(msg);
}

void FSM_Leds::update(
  const bool overheat,
  const bool av_missing,
  const bool power_off_bit,
  const bool quick_green_blink)
{
  switch (state) {
  case led_state::initial: // 0
    state_counter = 6;

    if (overheat) {
      state = led_state::overheat;
      return;
    }

    if (av_missing) {
      state = led_state::quick_green_orange;
      return;
    }

    if (power_off_bit) {
      state = led_state::off;
      return;
    }

    if (manual_control) {
      state = led_state::manual_control;
      return;
    }

    if (quick_green_blink) {
      state = led_state::quick_green_blink;
      return;
    }

    state = led_state::solid_green;
    break;

  case led_state::overheat: // 1
    // System overheated: slow blinking
    red_phases = 3;
    green_phases = 3;
    state = led_state::tick_update;
    break;

  case led_state::manual_control: // 2
    red_phases = red_phases_manual;
    green_phases = green_phases_manual;
    state = led_state::tick_update;
    break;

  case led_state::quick_green_blink: // 3
    // Quick green blinking
    red_phases = 0;
    green_phases = 5;
    state = led_state::tick_update;
    break;

  case led_state::solid_green: // 4
    red_phases = 0;
    green_phases = 0xF;
    state = led_state::tick_update;
    break;

  case led_state::off: // 5
    red_phases = 0;
    green_phases = 0;
    state = led_state::tick_update;
    break;

  case led_state::tick_update: // 6
    state_counter--;
    if (state_counter != 0) {
      return;
    }
    state = led_state::set_gpios;
    break;

  case led_state::set_gpios: // 7
    if (utils::checkBitNo(red_phases, state_counter)) {
      pico_hal::led_red_on();
    } else {
      pico_hal::led_red_off();
    }
    if (utils::checkBitNo(green_phases, state_counter)) {
      pico_hal::led_green_on();
    } else {
      pico_hal::led_green_off();
    }

    state_counter--;
    if (state_counter == -1) {
      state = led_state::reset_phase_counter;
    } else {
      state = led_state::initial;
    }
    break;

  case led_state::reset_phase_counter: // 8
    state_counter = 3; // ? gets overwritten by the initial state with 6...
    state = led_state::initial;
    break;

  case led_state::quick_green_orange: // 9
    red_phases = 5;
    green_phases = 0xF;
    state = led_state::tick_update;
    break;

  default:
    state = led_state::initial;
    break;
  }
}

} // namespace SMC
