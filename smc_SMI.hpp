#ifndef __SMC_SMI__
#define __SMC_SMI__

#include <cstdint>

namespace SMC {
namespace SMI {

enum class smi_power_state {
  decision_state,
  start_power_off,
  process_power_off_conditions,
  wait_for_os_power_off,
  event_interrupt_handled,
  os_busy_wait,
  request_tray_close,
  leds_off,
  wait_tray_close,
  initial,
  overheat_cooldown_wait,
  wait_for_os_response,
  going_to_reset,
  os_busy_limit,
  delay,
  wait_for_tray_stable,
  delayed_turning_off,
  wait_state_for_power_cycle
};

void init();
// True once after a power cycle (SMBus 0x02 bit 6), the main loop then
// presses the power button for the host
bool takePowerOnRequest();
// Same, without clearing it
bool powerOnRequested();
// P11 only: ticks off during a power cycle (SMBus 0x22), back to 25 in init()
void setPowerCycleDelay(uint8_t ticks);
void printState();
uint8_t update();

} // namespace SMI
} // namespace SMC

#endif // __SMC_SMI__
