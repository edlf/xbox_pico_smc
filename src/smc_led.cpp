#include "smc.hpp"
#include "hal.hpp"
#include "debug.hpp"

#include <iterator>
#include <string>

namespace SMC {

namespace Led {

static led_state state;
static uint8_t state_counter;
static uint8_t led_phase; // Phase bit shown next, 3 down to 0
static uint8_t green_phases_manual;
static uint8_t red_phases_manual;
static uint8_t green_phases;
static uint8_t red_phases;
static bool manual_control = false;
static bool overheat = false;
static bool av_missing = false;
static bool power_off_bit = false;
static bool quick_green_blink = false;

constexpr const char* state_names[] = {
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

void init() {
  state = led_state::resetPhaseCounter;
  state_counter = 0;
  led_phase = 0;
  green_phases_manual = 0;
  red_phases_manual = 0;
  green_phases = 0;
  red_phases = 0;
  manual_control = false;
}

void resetPhaseCounter() {
    state = led_state::resetPhaseCounter;
}

void clearPhases() {
    green_phases = 0;
    red_phases = 0;
}

void setGreenPhases(const uint8_t phase) {
    green_phases_manual = phase;
}

void setRedPhases(const uint8_t phase) {
    red_phases_manual = phase;
}

uint8_t getGreenPhases() {
    return green_phases_manual;
}

uint8_t getRedPhases() {
    return red_phases_manual;
}

void setManualControl(const bool i) {
    manual_control = i;
}

void setOverheat(const bool i) {
  overheat = i;
}

void setAvMissing(const bool i) {
  av_missing = i;
}

void setPowerOff(const bool i) {
  power_off_bit = i;
}

void setQuickGreenBlink(const bool i) {
  quick_green_blink = i;
}

void printState() {
    const size_t led_state = static_cast<size_t>(state);
    std::string msg = "LED state [" + std::to_string(led_state) + "]";

    if (led_state < std::size(state_names)) {
        msg += std::string(" ") + state_names[led_state];
    }

    debug::print_message(msg);
}

void update()
{
  switch (state) {
  case led_state::initial: // 0
    state_counter = 6;

    if (overheat) {
      state = led_state::overheat;
      return;
    }

#if SMC_FIRMWARE_P11
    // P11 checks the SMBus LED override before the AV cable and power off
    if (manual_control) {
      state = led_state::manual_control;
      return;
    }
#endif

    if (av_missing) {
      state = led_state::quick_green_orange;
      return;
    }

    if (power_off_bit) {
      state = led_state::off;
      return;
    }

#if !SMC_FIRMWARE_P11
    if (manual_control) {
      state = led_state::manual_control;
      return;
    }
#endif

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
    if (red_phases & (1 << led_phase)) {
      hal::led_red_on();
    } else {
      hal::led_red_off();
    }
    if (green_phases & (1 << led_phase)) {
      hal::led_green_on();
    } else {
      hal::led_green_off();
    }

    if (led_phase-- == 0) {
      state = led_state::resetPhaseCounter;
    } else {
      state = led_state::initial;
    }
    break;

  case led_state::resetPhaseCounter: // 8
    led_phase = 3;
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

} // namespace Led
} // namespace SMC
