#ifndef SMC_AUDIO_CLAMP_HPP
#define SMC_AUDIO_CLAMP_HPP

namespace SMC {
namespace AudioClamp {

enum class audio_state {
  clamped,
  tick_timer,
  unclamped
};

void init();
void update();
bool isClamped();
void printState();

} // namespace AudioClamp
} // SMC

#endif // SMC_AUDIO_CLAMP_HPP
