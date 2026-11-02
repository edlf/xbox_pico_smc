#include "smc_rgb_led.hpp"
#include "smc.hpp"
#include "hal.hpp"

namespace SMC {
namespace RgbLed {

constexpr uint16_t done_ticks = 250; // 5 s of green

static rgb_phase phase = rgb_phase::off;
static uint16_t done_timer = 0;

struct Colour {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

// Indexed by rgb_phase
constexpr Colour colours[] = {
  {0, 0, 0},     // off
  {255, 0, 255}, // power_on: magenta
  {0, 0, 255},   // bios: blue
  {0, 255, 255}, // kernel: cyan
  {0, 255, 0},   // done: green
  {0, 0, 0},     // idle
  {255, 0, 0},   // failed: red
};

static const char* const phase_names[] = {"Off", "Power on", "BIOS", "Kernel", "Booted", "Idle", "Failed"};

static void setPhase(const rgb_phase next) {
  if (next == phase) {
    return;
  }
  phase = next;
  if (phase == rgb_phase::done) {
    done_timer = done_ticks;
  }
  debug::print_debug(std::string("RGB: ") + phase_names[static_cast<uint8_t>(phase)]);
}

static void nextPhase() {
  if (!SMI::flags.system_running) {
    setPhase(rgb_phase::off);
    return;
  }
  if (BootChallenge::ramTestFailed() || BootChallenge::flags.frag_power_off) {
    setPhase(rgb_phase::failed);
    return;
  }
  if (hal::system_reset_asserted()) {
    setPhase(rgb_phase::power_on); // Also a reset from the dashboard: start over
    return;
  }

  const bool booted = BootChallenge::challengePassed() || SMI::flags.os_ready;
  switch (phase) {
  case rgb_phase::off:
  case rgb_phase::power_on:
  case rgb_phase::failed: // A retry got out of reset
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
  hal::set_rgb_led(0, 0, 0);
}

void update() {
  if (!SMC_RGB_LED) {
    return;
  }
  nextPhase();
  const Colour& c = colours[static_cast<uint8_t>(phase)];
  hal::set_rgb_led(c.red, c.green, c.blue);
}

} // namespace RgbLed
} // namespace SMC
