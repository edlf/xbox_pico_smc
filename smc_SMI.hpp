#ifndef __SMC_SMI__
#define __SMC_SMI__

#include <cstdint>

namespace SMC {
namespace SMI {

enum class smi_power_state {
  decision_state,
  start_power_off,
  case2,
  case3,
  event_interrupt_handled,
  case5,
  request_tray_close,
  leds_off,
  wait_tray_close,
  initial,
  overheat_cooldown_wait,
  case11,
  going_to_reset,
  case13,
  delay,
  case15,
  delayed_turning_off,
  wait_state_for_initial
};

void init();
void printState();
uint8_t update();
void setStateCase11();
void setStateGoingToReset();

} // namespace SMI
} // namespace SMC

#endif // __SMC_SMI__
