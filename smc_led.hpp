#ifndef __smc_led__
#define __smc_led__

#include "smc_types.hpp"

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
  quick_green_orange
};

void init();
void update();
void resetPhaseCounter();
void resetState();
void setGreenPhases(const uint8_t);
void setRedPhases(const uint8_t);
void setManualControl(const bool);
void setOverheat(const bool);
void setAvMissing(const bool);
void setPowerOff(const bool);
void setQuickGreenBlink(const bool);
void printState();

} // namespace Led
} // namespace SMC

#endif // __smc_led__
