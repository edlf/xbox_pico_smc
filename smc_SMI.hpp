#ifndef __SMC_SMI__
#define __SMC_SMI__

#include <cstdint>

namespace SMC {
namespace SMI {

enum class smi_power_state {
  decision_state,
  start_power_off,
  process_power_off_conditions,
  check_ram_test_results,
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
  wait_for_tray_stable,
  delayed_turning_off,
  wait_state_for_power_cycle
};

void init();
void printState();
uint8_t update();
void setStateCase11();
void setStateGoingToReset();
bool isXboxPowered();

} // namespace SMI
} // namespace SMC

#endif // __SMC_SMI__
