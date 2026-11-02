#ifndef SMC_AUDIO_CLAMP_HPP
#define SMC_AUDIO_CLAMP_HPP

namespace SMC {
namespace AudioClamp {

enum class audio_state { clamped, tick_timer, unclamped };

// Audio clamp flags
struct flags_struct {
  bool audio_clamp_timer;      // Unclamp the audio after a delay (set at power on)
  bool audio_clamp_request;    // SMBus 0x0B = 1
  bool audio_unclamp_request;  // SMBus 0x0B = 0, or the AV cable is back
};
extern flags_struct flags;

void init();
void update();
bool isClamped();
void printState();

}  // namespace AudioClamp
}  // namespace SMC

#endif  // SMC_AUDIO_CLAMP_HPP
