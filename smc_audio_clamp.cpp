#include "smc_audio_clamp.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"

#include <vector>
#include <string>

namespace SMC {
namespace AudioClamp {

audio_state state;
uint16_t audio_clamp_timeout;

const std::vector<std::string> state_names {
  "Clamped",
  "Tick timer",
  "Unclamped"
};

void init() {
  state = audio_state::clamped;
  audio_clamp_timeout = 0;
}

void update() {
  switch (state) {
  case audio_state::clamped:
    // Audio clamped
    hal::audio_clamp_on();
    utils::clearBitNo(flags.bitfield_DATA_6F, 0);
    audio_clamp_timeout = 44;

    // Cable missing, keep audio clamped
    if (utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      return;
    }

    // Check if clamp off was requested
    if (utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
      if (!checkStatusBit(audio_clamp_timer)) {
        return;
      }
      state = audio_state::tick_timer;
      return;
    }
    state = audio_state::unclamped;
    break;

  case audio_state::tick_timer:
    // Audio clamped timer tick
    clearStatusBit(audio_clamp_timer);

    if (!utils::checkBitNo(flags.bitfield_DATA_71, 6) && !utils::checkBitNo(flags.bitfield_DATA_6F, 0) && !utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
      if (!utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
        audio_clamp_timeout--;
        if (audio_clamp_timeout != 0) {
          return;
        }
      }

      state = audio_state::unclamped;
    } else {
      state = audio_state::clamped;
    }
    break;

  case audio_state::unclamped:
    // Audio unclamped
    hal::audio_clamp_off();
    utils::clearBitNo(flags.bitfield_DATA_6F, 7);
    clearStatusBit(audio_clamp_timer);

    if (checkStatusBit(prepare_for_shutdown) && !utils::checkBitNo(flags.bitfield_DATA_6F, 0) && !utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      return;
    }
    state = audio_state::clamped;
    break;

  default:
    state = audio_state::clamped;
    break;
  }
}

bool isClamped() {
  return state == audio_state::clamped;
}

void clamp() {
  state = audio_state::clamped;
  hal::audio_clamp_on();
}

void unclamp() {
  state = audio_state::unclamped;
  hal::audio_clamp_off();
}


void printState() {
    const size_t led_state = static_cast<size_t>(state);
    std::string msg = "LED state [" + std::to_string(led_state) + "]";

    if (led_state <= state_names.size()) {
        msg += " " + state_names[led_state];
    }

    debug::print_message(msg);
}

} // AudioClamp
} // namespace SMC
