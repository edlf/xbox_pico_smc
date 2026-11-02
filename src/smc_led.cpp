#include "smc.hpp"
#include "hal.hpp"
#include "debug.hpp"
#include "hardware/sync.h"

#include <iterator>
#include <string>

namespace SMC {

namespace Led {

flags_struct flags;

static led_state state;
static uint8_t   state_counter;
static uint8_t   led_phase;  // Phase bit shown next, 3 down to 0
static uint8_t   green_phases_manual;
static uint8_t   red_phases_manual;
static uint8_t   green_phases;
static uint8_t   red_phases;
static bool      manual_control = false;
static bool      overheat = false;
static bool      av_missing = false;
static bool      power_off_bit = true;
static bool      quick_green_blink = false;
static bool      boot_failure = false;
static uint8_t   red_phases_failure;
static uint8_t   green_phases_failure;

// With SMC_LED_FADE the LEDs fade in when the Xbox powers up and fade out the
// last pattern when it powers down, instead of switching at once like the PIC
enum class fade_direction { none, in, out };
constexpr uint8_t     fade_in_ticks = 50;   // 1 s
constexpr uint8_t     fade_out_ticks = 25;  // 0.5 s, power off waits for it
static fade_direction fade = fade_direction::none;
static uint8_t        fade_step;          // 0 to fade_in_ticks or fade_out_ticks
static uint8_t        red_phases_fading;  // Pattern shown while fading out
static uint8_t        green_phases_fading;
// Faded out: dark until the power is off, so a pattern that wins over "off"
// (the SMBus LED override) can't light up again while the tray closes
static bool dark_until_power_off = false;

constexpr const char* state_names[] = {"Initial",     "Overheat",    "Manual control", "Quick green blink",   "Solid green",
                                       "Off",         "Tick update", "Set GPIOs",      "Reset phase counter", "Quick green/orange (Missing AV)",
                                       "Boot failure"};

void init() {
  state = led_state::resetPhaseCounter;
  state_counter = 0;
  led_phase = 0;
  green_phases_manual = 0;
  red_phases_manual = 0;
  green_phases = 0;
  red_phases = 0;
  manual_control = false;
  red_phases_failure = 0;
  green_phases_failure = 0;
  power_off_bit = true;
  fade = fade_direction::none;
  dark_until_power_off = false;
  hal::set_led_fade(255);
}

// Called by the main loop once the Xbox is off
void resetPhaseCounter() {
  state = led_state::resetPhaseCounter;
  dark_until_power_off = false;
}

void clearPhases() {
  if (fade == fade_direction::out) {
    return;  // The fade turns them off
  }
  green_phases = 0;
  red_phases = 0;
}

// Brightness for a fade step out of ticks: squared, so the change looks even
static uint8_t fadeLevel(uint8_t step, uint8_t ticks) {
  return static_cast<uint8_t>((uint32_t)255 * step * step / (ticks * ticks));
}

// Called once per tick
static void stepFade() {
  switch (fade) {
    case fade_direction::none:
      break;

    case fade_direction::in:
      if (++fade_step >= fade_in_ticks) {
        fade = fade_direction::none;
        hal::set_led_fade(255);
      } else {
        hal::set_led_fade(fadeLevel(fade_step, fade_in_ticks));
      }
      break;

    case fade_direction::out:
      if (fade_step == 0 || --fade_step == 0) {
        // Dark: switch off, then back to full brightness for what comes next
        fade = fade_direction::none;
        dark_until_power_off = true;
        hal::led_red_off();
        hal::led_green_off();
        hal::set_led_fade(255);
      } else {
        hal::set_led_fade(fadeLevel(fade_step, fade_out_ticks));
      }
      break;
  }
}

bool isFadingOut() {
  return fade == fade_direction::out;
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
  if (!SMC_LED_FADE) {
    // Switched at once, like the PIC
  } else if (i && !power_off_bit) {
    // Powering down: keep showing the current pattern while it fades out,
    // from wherever a fade in had got to
    red_phases_fading = red_phases;
    green_phases_fading = green_phases;
    if (fade == fade_direction::in) {
      fade_step = (uint16_t)fade_step * fade_out_ticks / fade_in_ticks;  // Same level
    } else {
      fade_step = fade_out_ticks;
    }
    fade = fade_direction::out;
  } else if (!i && power_off_bit) {
    // Powering up: fade in from dark (or from where a fade out had got to)
    dark_until_power_off = false;
    if (fade == fade_direction::out) {
      fade_step = (uint16_t)fade_step * fade_in_ticks / fade_out_ticks;  // Same level
    } else {
      fade_step = 0;
    }
    fade = fade_direction::in;
    hal::set_led_fade(fadeLevel(fade_step, fade_in_ticks));
  }
  power_off_bit = i;
}

void setQuickGreenBlink(const bool i) {
  quick_green_blink = i;
}

void setBootFailure(const bool i) {
  boot_failure = i;
}

void setBootFailurePattern(const uint8_t red, const uint8_t green) {
  red_phases_failure = red;
  green_phases_failure = green;
}

void printState() {
  const size_t led_state = static_cast<size_t>(state);
  std::string  msg = "LED state [" + std::to_string(led_state) + "]";

  if (led_state < std::size(state_names)) {
    msg += std::string(" ") + state_names[led_state];
  }

  debug::print_message(msg);
}

static volatile uint8_t requested_brightness = 0;  // 0: none

void requestBrightness(const uint8_t percent) {
  requested_brightness = percent;
}

void update() {
  const uint32_t irq_state = save_and_disable_interrupts();  // Take it without losing a newer one
  const uint8_t brightness = requested_brightness;
  requested_brightness = 0;
  restore_interrupts(irq_state);
  if (brightness != 0) {
    if (brightness != hal::get_led_brightness()) {
      hal::set_led_brightness(brightness);
      debug::print_message("LED: Brightness " + std::to_string(brightness) + "% (SMBus 0x33)");
    }
  }

  stepFade();

  switch (state) {
    case led_state::initial:  // 0
      state_counter = 6;

      if (dark_until_power_off) {
        state = led_state::off;
        return;
      }

      // The boot failure error code wins over everything, an overheat and the
      // SMBus LED override included (the PIC shows it through the override, so a
      // later SMBus 0x07/0x08 write or an overheat could hide it)
      if (boot_failure) {
        state = led_state::boot_failure;
        return;
      }

      if (overheat) {
        state = led_state::overheat;
        return;
      }

      // A missing AV cable wins over the SMBus LED override on every firmware, so
      // a dashboard that sets its own LEDs can't hide it (P11 checks the override
      // first)
      if (av_missing) {
        state = led_state::quick_green_orange;
        return;
      }

      // The SMBus LED override comes before power off, as P11/P2L (P01 checks
      // power off first)
      if (manual_control) {
        state = led_state::manual_control;
        return;
      }

      if (power_off_bit) {
        state = led_state::off;
        return;
      }

      if (quick_green_blink) {
        state = led_state::quick_green_blink;
        return;
      }

      state = led_state::solid_green;
      break;

    case led_state::overheat:  // 1
      // System overheated: slow blinking
      red_phases = 3;
      green_phases = 3;
      state = led_state::tick_update;
      break;

    case led_state::manual_control:  // 2
      red_phases = red_phases_manual;
      green_phases = green_phases_manual;
      state = led_state::tick_update;
      break;

    case led_state::quick_green_blink:  // 3
      // Quick green blinking
      red_phases = 0;
      green_phases = 5;
      state = led_state::tick_update;
      break;

    case led_state::solid_green:  // 4
      red_phases = 0;
      green_phases = 0xF;
      state = led_state::tick_update;
      break;

    case led_state::off:  // 5
      if (fade == fade_direction::out) {
        red_phases = red_phases_fading;
        green_phases = green_phases_fading;
      } else {
        red_phases = 0;
        green_phases = 0;
      }
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

    case led_state::resetPhaseCounter:  // 8
      led_phase = 3;
      state = led_state::initial;
      break;

    case led_state::quick_green_orange:  // 9
      red_phases = 5;
      green_phases = 0xF;
      state = led_state::tick_update;
      break;

    case led_state::boot_failure:  // 10
      red_phases = red_phases_failure;
      green_phases = green_phases_failure;
      state = led_state::tick_update;
      break;

    default:
      state = led_state::initial;
      break;
  }
}

}  // namespace Led
}  // namespace SMC
