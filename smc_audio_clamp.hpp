#ifndef __SMC_AUDIO_CLAMP__
#define __SMC_AUDIO_CLAMP__

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
void clamp();
void unclamp();
void printState();

} // AudioClamp
} // SMC

#endif // __SMC_AUDIO_CLAMP__
