#ifndef __SMC_FAN__
#define __SMC_FAN__

#include <cstdint>

namespace SMC {
namespace Fan {

enum class fan_control_state {
  initial,
  start,
  decision_state,
  overheated,
  overheated_cooldown,
  overheated_wait,
  custom_fan_speed,
  board_cool,
  board_hot,
  wait,
  start_delay,
  sample_temperatures,
  cpu_hot,
  start_sampling
};

void init();
void update_fan_temp();
void powerOff();
uint8_t getFanSpeed();
void setCustomSpeed(const uint8_t);
uint8_t getCustomFanSpeed();

} // namespace Fan
} // namespace SMC

#endif // __SMC_FAN__
