#ifndef SMC_LED_HPP
#define SMC_LED_HPP

#include <cstdint>

namespace SMC {
namespace Led {

enum class led_state {
  initial,
  overheat,
  manual_control,
  quick_green_blink,
  solid_green,
  off,
  tick_update,
  set_gpios,
  resetPhaseCounter,
  quick_green_orange,
  boot_failure
};

void init();
void update();
void resetPhaseCounter();
// LEDs off until the next pattern is picked
void clearPhases();
// Phases shown while LEDs are under manual control (SMBus 0x08, boot error codes)
void setGreenPhases(const uint8_t);
void setRedPhases(const uint8_t);
uint8_t getGreenPhases();
uint8_t getRedPhases();
void setManualControl(const bool);
void setOverheat(const bool);
void setAvMissing(const bool);
void setPowerOff(const bool);
// SMC_LED_FADE: the LEDs are still fading out after power down started
bool isFadingOut();
void setQuickGreenBlink(const bool);
// Boot failure (FRAG) error code, shown above every other pattern
void setBootFailure(const bool);
void setBootFailurePattern(const uint8_t red, const uint8_t green);
void printState();

} // namespace Led
} // namespace SMC

#endif // SMC_LED_HPP
