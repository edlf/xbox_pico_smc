#include "smc.hpp"
#include "hal.hpp"
#include "debug.hpp"

#include <iterator>
#include <string>

namespace SMC {

namespace Led {

flags_struct flags;

// A blink pattern: 4 phases per colour, bit 3 shown first. Both on is orange.
struct Pattern {
  uint8_t red = 0;
  uint8_t green = 0;
};

constexpr Pattern dark_pattern = {0x0, 0x0};
constexpr Pattern overheat_pattern = {0x3, 0x3};           // Slow orange blink
constexpr Pattern quick_green_blink_pattern = {0x0, 0x5};  // Tray moving
constexpr Pattern solid_green_pattern = {0x0, 0xF};
constexpr Pattern quick_green_orange_pattern = {0x5, 0xF};  // No AV cable

static led_state state;
static uint8_t   state_counter;
static uint8_t   led_phase;  // Phase bit shown next, 3 down to 0
static Pattern   pattern;    // Being shown
static Pattern   manual_pattern;
static Pattern   failure_pattern;

// What picks the pattern, set by update_LEDs() every tick
struct Inputs {
  bool manual_control = false;  // SMBus LED override
  bool overheat = false;
  bool av_missing = false;
  bool power_off = true;
  bool quick_green_blink = false;
  bool boot_failure = false;
};
static Inputs inputs;

// With SMC_LED_FADE the LEDs fade in when the Xbox powers up and fade out the
// last pattern when it powers down, instead of switching at once like the PIC
enum class fade_direction { none, in, out };
constexpr uint8_t fade_in_ticks = 50;   // 1 s
constexpr uint8_t fade_out_ticks = 25;  // 0.5 s, power off waits for it
struct Fade {
  fade_direction direction = fade_direction::none;
  uint8_t        step = 0;  // 0 to fade_in_ticks or fade_out_ticks
  Pattern        pattern;   // Shown while fading out
  // Faded out: dark until the power is off, so a pattern that wins over "off"
  // (the SMBus LED override) can't light up again while the tray closes
  bool dark_until_power_off = false;
};
static Fade fade;

constexpr const char* state_names[] = {"Initial",     "Overheat",    "Manual control", "Quick green blink",   "Solid green",
                                       "Off",         "Tick update", "Set GPIOs",      "Reset phase counter", "Quick green/orange (Missing AV)",
                                       "Boot failure"};

void init() {
  state = led_state::resetPhaseCounter;
  state_counter = 0;
  led_phase = 0;
  pattern = {};
  manual_pattern = {};
  failure_pattern = {};
  inputs.manual_control = false;
  inputs.power_off = true;
  fade = {};
  hal::set_led_fade(255);
}

// Called by the main loop once the Xbox is off
void resetPhaseCounter() {
  state = led_state::resetPhaseCounter;
  fade.dark_until_power_off = false;
}

void clearPhases() {
  if (fade.direction == fade_direction::out) {
    return;  // The fade turns them off
  }
  pattern = dark_pattern;
}

// Brightness for a fade step out of ticks: squared, so the change looks even
static uint8_t fadeLevel(uint8_t step, uint8_t ticks) {
  return static_cast<uint8_t>(uint32_t{255} * step * step / (ticks * ticks));
}

// Called once per tick
static void stepFade() {
  switch (fade.direction) {
    case fade_direction::none:
      break;

    case fade_direction::in:
      if (++fade.step >= fade_in_ticks) {
        fade.direction = fade_direction::none;
        hal::set_led_fade(255);
      } else {
        hal::set_led_fade(fadeLevel(fade.step, fade_in_ticks));
      }
      break;

    case fade_direction::out:
      if (fade.step == 0 || --fade.step == 0) {
        // Dark: switch off, then back to full brightness for what comes next
        fade.direction = fade_direction::none;
        fade.dark_until_power_off = true;
        hal::led_red_off();
        hal::led_green_off();
        hal::set_led_fade(255);
      } else {
        hal::set_led_fade(fadeLevel(fade.step, fade_out_ticks));
      }
      break;
  }
}

bool isFadingOut() {
  return fade.direction == fade_direction::out;
}

void setGreenPhases(const uint8_t phase) {
  manual_pattern.green = phase;
}

void setRedPhases(const uint8_t phase) {
  manual_pattern.red = phase;
}

uint8_t getGreenPhases() {
  return manual_pattern.green;
}

uint8_t getRedPhases() {
  return manual_pattern.red;
}

void setManualControl(const bool i) {
  inputs.manual_control = i;
}

void setOverheat(const bool i) {
  inputs.overheat = i;
}

void setAvMissing(const bool i) {
  inputs.av_missing = i;
}

void setPowerOff(const bool i) {
  if (!SMC_LED_FADE) {
    // Switched at once, like the PIC
  } else if (i && !inputs.power_off) {
    // Powering down: keep showing the current pattern while it fades out,
    // from wherever a fade in had got to
    fade.pattern = pattern;
    if (fade.direction == fade_direction::in) {
      fade.step = static_cast<uint8_t>(fade.step * fade_out_ticks / fade_in_ticks);  // Same level
    } else {
      fade.step = fade_out_ticks;
    }
    fade.direction = fade_direction::out;
  } else if (!i && inputs.power_off) {
    // Powering up: fade in from dark (or from where a fade out had got to)
    fade.dark_until_power_off = false;
    if (fade.direction == fade_direction::out) {
      fade.step = static_cast<uint8_t>(fade.step * fade_in_ticks / fade_out_ticks);  // Same level
    } else {
      fade.step = 0;
    }
    fade.direction = fade_direction::in;
    hal::set_led_fade(fadeLevel(fade.step, fade_in_ticks));
  }
  inputs.power_off = i;
}

void setQuickGreenBlink(const bool i) {
  inputs.quick_green_blink = i;
}

void setBootFailure(const bool i) {
  inputs.boot_failure = i;
}

void setBootFailurePattern(const uint8_t red, const uint8_t green) {
  failure_pattern = {red, green};
}

void printState() {
  const size_t led_state = static_cast<size_t>(state);
  std::string  msg = "LED state [" + std::to_string(led_state) + "]";

  if (led_state < std::size(state_names)) {
    msg += std::string(" ") + state_names[led_state];
  }

  debug::print_message(msg);
}

void update() {
  stepFade();

  switch (state) {
    case led_state::initial:  // 0
      state_counter = 6;

      if (fade.dark_until_power_off) {
        state = led_state::off;
        return;
      }

      // The boot failure error code wins over everything, an overheat and the
      // SMBus LED override included (the PIC shows it through the override, so a
      // later SMBus 0x07/0x08 write or an overheat could hide it)
      if (inputs.boot_failure) {
        state = led_state::boot_failure;
        return;
      }

      if (inputs.overheat) {
        state = led_state::overheat;
        return;
      }

      // A missing AV cable wins over the SMBus LED override on every firmware, so
      // a dashboard that sets its own LEDs can't hide it (P11 checks the override
      // first)
      if (inputs.av_missing) {
        state = led_state::quick_green_orange;
        return;
      }

      // The SMBus LED override comes before power off, as P11/P2L (P01 checks
      // power off first)
      if (inputs.manual_control) {
        state = led_state::manual_control;
        return;
      }

      if (inputs.power_off) {
        state = led_state::off;
        return;
      }

      if (inputs.quick_green_blink) {
        state = led_state::quick_green_blink;
        return;
      }

      state = led_state::solid_green;
      break;

    case led_state::overheat:  // 1
      pattern = overheat_pattern;
      state = led_state::tick_update;
      break;

    case led_state::manual_control:  // 2
      pattern = manual_pattern;
      state = led_state::tick_update;
      break;

    case led_state::quick_green_blink:  // 3
      pattern = quick_green_blink_pattern;
      state = led_state::tick_update;
      break;

    case led_state::solid_green:  // 4
      pattern = solid_green_pattern;
      state = led_state::tick_update;
      break;

    case led_state::off:  // 5
      pattern = fade.direction == fade_direction::out ? fade.pattern : dark_pattern;
      state = led_state::tick_update;
      break;

    case led_state::tick_update:  // 6
      state_counter--;
      if (state_counter != 0) {
        return;
      }
      state = led_state::set_gpios;
      break;

    case led_state::set_gpios:  // 7
      if (pattern.red & (1 << led_phase)) {
        hal::led_red_on();
      } else {
        hal::led_red_off();
      }
      if (pattern.green & (1 << led_phase)) {
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

    case led_state::resetPhaseCounter:  // 8
      led_phase = 3;
      state = led_state::initial;
      break;

    case led_state::quick_green_orange:  // 9
      pattern = quick_green_orange_pattern;
      state = led_state::tick_update;
      break;

    case led_state::boot_failure:  // 10
      pattern = failure_pattern;
      state = led_state::tick_update;
      break;

    default:
      state = led_state::initial;
      break;
  }
}

}  // namespace Led
}  // namespace SMC
