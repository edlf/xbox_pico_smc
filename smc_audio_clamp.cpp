#include "smc_audio_clamp.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"

#include <vector>
#include <string>

namespace SMC {
namespace AudioClamp {

// Same as the PIC (0x573). Requests: audio_clamp_request, audio_unclamp_request (SMBus
// 0x0B), audio_clamp_timer = unclamp after a delay (set at power up). Missing AV cable
// (av_cable_missing) or a shutdown (prepare_for_shutdown) always clamps.
constexpr uint8_t unclamp_delay = 0x2C; // 44 ticks, ~0.9 s

audio_state state;
uint8_t audio_clamp_timeout;

const std::vector<std::string> state_names {
  "Clamped",
  "Tick timer",
  "Unclamped"
};

void init() {
  state = audio_state::clamped;
  audio_clamp_timeout = 0;
}

static bool mustClamp() {
  return flags.av_cable_missing || flags.audio_clamp_request ||
         flags.prepare_for_shutdown;
}

void update() {
  switch (state) {
  case audio_state::clamped:
    hal::audio_clamp_on();
    flags.audio_clamp_request = false;
    audio_clamp_timeout = unclamp_delay;

    if (flags.av_cable_missing) {
      return; // No AV cable
    }
    if (flags.audio_unclamp_request) {
      state = audio_state::unclamped;
    } else if (flags.audio_clamp_timer) {
      state = audio_state::tick_timer;
    }
    break;

  case audio_state::tick_timer:
    flags.audio_clamp_timer = false;
    if (mustClamp()) {
      state = audio_state::clamped;
    } else if (flags.audio_unclamp_request || --audio_clamp_timeout == 0) {
      state = audio_state::unclamped;
    }
    break;

  case audio_state::unclamped:
    hal::audio_clamp_off();
    flags.audio_unclamp_request = false;
    flags.audio_clamp_timer = false;
    if (mustClamp()) {
      state = audio_state::clamped;
    }
    break;

  default:
    state = audio_state::clamped;
    break;
  }
}

bool isClamped() {
  return state == audio_state::clamped;
}

void printState() {
    const size_t audio_state = static_cast<size_t>(state);
    std::string msg = "Audio clamp state [" + std::to_string(audio_state) + "]";

    if (audio_state <= state_names.size()) {
        msg += " " + state_names[audio_state];
    }

    debug::print_message(msg);
}

} // AudioClamp
} // namespace SMC
