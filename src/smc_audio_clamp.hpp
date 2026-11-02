#ifndef SMC_AUDIO_CLAMP_HPP
#define SMC_AUDIO_CLAMP_HPP

namespace SMC {
namespace AudioClamp {

enum class audio_state { clamped, tick_timer, unclamped };

// Audio clamp flags, from the PIC's RAM (address.bit)
struct flags_struct {
  bool audio_clamp_timer;      // 6E.3 Unclamp the audio after a delay (set at power on)
  bool audio_clamp_request;    // 6F.0 SMBus 0x0B = 1
  bool audio_unclamp_request;  // 6F.7 SMBus 0x0B = 0, or the AV cable is back
};
extern flags_struct flags;

void init();
void update();
bool isClamped();
void printState();

}  // namespace AudioClamp
}  // namespace SMC

#endif  // SMC_AUDIO_CLAMP_HPP
