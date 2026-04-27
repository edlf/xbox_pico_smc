#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"

namespace SMC {
namespace AudioClamp {

uint16_t audio_clamp_timeout;

void init() {
  state.audio_clamp = audio_state::clamped;
  audio_clamp_timeout = 0;
}

void update() {
  switch (state.audio_clamp) {
  case audio_state::clamped:
    // Audio clamped
    pico_hal::audio_clamp_on();
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
      state.audio_clamp = audio_state::tick_timer;
      return;
    }
    state.audio_clamp = audio_state::unclamped;
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

      state.audio_clamp = audio_state::unclamped;
    } else {
      state.audio_clamp = audio_state::clamped;
    }
    break;

  case audio_state::unclamped:
    // Audio unclamped
    pico_hal::audio_clamp_off();
    utils::clearBitNo(flags.bitfield_DATA_6F, 7);
    clearStatusBit(audio_clamp_timer);

    if (checkStatusBit(prepare_for_shutdown) && !utils::checkBitNo(flags.bitfield_DATA_6F, 0) && !utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      return;
    }
    state.audio_clamp = audio_state::clamped;
    break;

  default:
    state.audio_clamp = audio_state::clamped;
    break;
  }
}

} // AudioClamp
} // namespace SMC
