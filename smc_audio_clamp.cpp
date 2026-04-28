#include "smc_audio_clamp.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"

#include <vector>
#include <string>

namespace SMC {
namespace AudioClamp {

// Same as the PIC (0x573). Requests: 6F.0 = clamp, 6F.7 = unclamp (SMBus
// 0x0B), 6E.3 = unclamp after a delay (set at power up). Missing AV cable
// (71.6) or a shutdown (6E.5) always clamps.
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
  return utils::checkBitNo(flags.bitfield_DATA_71, 6) || utils::checkBitNo(flags.bitfield_DATA_6F, 0) ||
         checkStatusBit(prepare_for_shutdown);
}

void update() {
  switch (state) {
  case audio_state::clamped:
    hal::audio_clamp_on();
    utils::clearBitNo(flags.bitfield_DATA_6F, 0);
    audio_clamp_timeout = unclamp_delay;

    if (utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      return; // No AV cable
    }
    if (utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
      state = audio_state::unclamped;
    } else if (checkStatusBit(audio_clamp_timer)) {
      state = audio_state::tick_timer;
    }
    break;

  case audio_state::tick_timer:
    clearStatusBit(audio_clamp_timer);
    if (mustClamp()) {
      state = audio_state::clamped;
    } else if (utils::checkBitNo(flags.bitfield_DATA_6F, 7) || --audio_clamp_timeout == 0) {
      state = audio_state::unclamped;
    }
    break;

  case audio_state::unclamped:
    hal::audio_clamp_off();
    utils::clearBitNo(flags.bitfield_DATA_6F, 7);
    clearStatusBit(audio_clamp_timer);
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
