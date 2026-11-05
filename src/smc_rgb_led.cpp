#include "smc_rgb_led.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include <cstring>

namespace SMC {
namespace RgbLed {

constexpr uint16_t done_ticks = 250;  // 5 s of green

static rgb_phase phase = rgb_phase::off;
static uint16_t  done_timer = 0;

// Settings: kept over resets and power offs, until the Pico restarts (or saved)
static Mode    current_mode = static_cast<Mode>(SMC_RGB_MODE);
static uint8_t brightness_percent = SMC_RGB_BRIGHTNESS;

static const char* const mode_names[] = {"off", "boot", "mirror"};
static_assert(sizeof(mode_names) / sizeof(mode_names[0]) == mode_count, "One name per Mode");

const char* modeName(const Mode m) {
  const uint8_t i = static_cast<uint8_t>(m);
  return i < mode_count ? mode_names[i] : "?";
}

bool parseMode(const char* name, Mode& m) {
  for (uint8_t i = 0; i < mode_count; i++) {
    if (strcmp(name, mode_names[i]) == 0) {
      m = static_cast<Mode>(i);
      return true;
    }
  }
  return false;
}

Mode mode() {
  return current_mode;
}

void setMode(const Mode m) {
  if (static_cast<uint8_t>(m) < mode_count) {
    current_mode = m;
  }
}

uint8_t brightness() {
  return brightness_percent;
}

bool setBrightness(const uint8_t percent) {
  if (percent < 1 || percent > 100) {
    return false;
  }
  brightness_percent = percent;
  return true;
}

struct Colour {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

// Indexed by rgb_phase
constexpr Colour colours[] = {
    {0,   0,   0  }, // off
    {255, 0,   255}, // power_on: magenta
    {0,   0,   255}, // bios: blue
    {0,   255, 255}, // kernel: cyan
    {0,   255, 0  }, // done: green
    {0,   0,   0  }, // idle
    {255, 0,   0  }, // failed: red
};

// A failed RAM test shows the colour of the chip at fault instead of red:
// chips 0-3 steady, 4-7 (128 MB boards) the same colours blinking. With
// several it steps through them, 1 s each.
constexpr Colour chip_colours[] = {
    {255, 255, 0  }, // Chip 0 / 4: yellow
    {255, 64,  0  }, // Chip 1 / 5: orange
    {128, 0,   255}, // Chip 2 / 6: purple
    {255, 255, 255}, // Chip 3 / 7: white
};
constexpr uint8_t chip_ticks = 50;   // 1 s per chip
constexpr uint8_t blink_ticks = 12;  // Chips 4-7: on and off ~4 times a second
static uint8_t    chip_timer = 0;
static uint8_t    chip_shown = 0;

static Colour failedColour() {
  const uint8_t chips = BootChallenge::failedRamChips();
  if (chips == 0) {
    return colours[static_cast<uint8_t>(rgb_phase::failed)];  // FRAG, or no chip flagged
  }
  if (chip_timer == 0 || !(chips & (1u << chip_shown))) {
    // Next chip at fault
    for (uint8_t i = 1; i <= 8; i++) {
      const uint8_t chip = (chip_shown + i) % 8;
      if (chips & (1u << chip)) {
        chip_shown = chip;
        break;
      }
    }
    chip_timer = chip_ticks;
  }
  chip_timer--;
  if (chip_shown >= 4 && (chip_timer / (blink_ticks / 2)) % 2 != 0) {
    return Colour{0, 0, 0};
  }
  return chip_colours[chip_shown % 4];
}

// The colour set over SMBus 0x37-0x3A
struct SmbusColour {
  bool    shown = false;
  uint8_t value[3] = {};  // Red, green, blue
};
static SmbusColour smbus;

void showSmbusColour(const bool show) {
  smbus.shown = show;
}

bool smbusColourShown() {
  return smbus.shown;
}

void setSmbusColour(const uint8_t channel, const uint8_t value) {
  if (channel < 3) {
    smbus.value[channel] = value;
  }
}

uint8_t smbusColour(const uint8_t channel) {
  return channel < 3 ? smbus.value[channel] : 0;
}

// Mirror mode after booting: what the front panel LEDs show
static Colour frontPanelColour() {
  const bool red = Led::redLit();
  const bool green = Led::greenLit();
  if (red && green) {
    return {255, 80, 0};  // Orange
  }
  if (red) {
    return {255, 0, 0};
  }
  if (green) {
    return {0, 255, 0};
  }
  return {0, 0, 0};
}

static const char* const phase_names[] = {"Off", "Power on", "BIOS", "Kernel", "Booted", "Idle", "Failed"};

static void setPhase(const rgb_phase next) {
  if (next == phase) {
    return;
  }
  phase = next;
  if (phase == rgb_phase::done) {
    done_timer = done_ticks;
  }
  debug::print(debug::Level::Debug, "RGB: %s", phase_names[static_cast<uint8_t>(phase)]);
}

static void nextPhase() {
  if (!SMI::flags.system_running) {
    setPhase(rgb_phase::off);
    smbus.shown = false;  // Like the front panel's SMBus override (0x07)
    return;
  }
  if (BootChallenge::ramTestFailed() || BootChallenge::flags.frag_power_off) {
    setPhase(rgb_phase::failed);
    return;
  }
  if (hal::system_reset_asserted()) {
    setPhase(rgb_phase::power_on);  // Also a reset from the dashboard: start over
    smbus.shown = false;
    return;
  }

  const bool booted = BootChallenge::challengePassed() || SMI::flags.os_ready;
  switch (phase) {
    case rgb_phase::off:
    case rgb_phase::power_on:
    case rgb_phase::failed:  // A retry got out of reset
      setPhase(rgb_phase::bios);
      break;

    case rgb_phase::bios:
      if (booted) {
        setPhase(rgb_phase::done);
      } else if (BootChallenge::flags.ram_test_submitted && BootChallenge::flags.ram_test_passed) {
        setPhase(rgb_phase::kernel);
      }
      break;

    case rgb_phase::kernel:
      if (booted) {
        setPhase(rgb_phase::done);
      }
      break;

    case rgb_phase::done:
      if (--done_timer == 0) {
        setPhase(rgb_phase::idle);
      }
      break;

    case rgb_phase::idle:
      break;
  }
}

void init() {
  phase = rgb_phase::off;
  done_timer = 0;
  chip_timer = 0;
  chip_shown = 0;
  smbus = {};
  hal::set_rgb_led(0, 0, 0);
}

static uint8_t scaled(const uint8_t value) {
  return static_cast<uint8_t>(value * brightness_percent / 100);
}

void update() {
  if (!SMC_RGB_LED) {
    return;
  }
  nextPhase();

  Colour c = {0, 0, 0};
  if (current_mode == Mode::off) {
    // Dark, whatever else asks for a colour
  } else if (smbus.shown) {
    c = {smbus.value[0], smbus.value[1], smbus.value[2]};
  } else if (phase == rgb_phase::failed) {
    c = failedColour();
  } else if (phase == rgb_phase::idle && current_mode == Mode::mirror) {
    c = frontPanelColour();
  } else {
    c = colours[static_cast<uint8_t>(phase)];
  }
  hal::set_rgb_led(scaled(c.red), scaled(c.green), scaled(c.blue));
}

}  // namespace RgbLed
}  // namespace SMC
