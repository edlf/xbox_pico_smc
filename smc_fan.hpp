#ifndef __SMC_FAN__
#define __SMC_FAN__

#include <cstdint>

namespace SMC {
namespace Fan {

constexpr uint8_t MAX_BOARD_TEMP = 75;
constexpr uint8_t MAX_CPU_TEMP = 88;
constexpr uint8_t temperature_history_length = 16;

enum class fan_control_state {
  initial,
  state1,
  decision_state,
  overheated,
  overheated_cooldown,
  state5,
  custom_fan_speed,
  board_cool,
  board_hot,
  state9,
  state10,
  state11,
  cpu_hot,
  state13
};

void init();
void update_fan_temp();
void setFanSpeed(const uint8_t);
uint8_t getFanSpeed();
void applyFanSpeed();
void setCustomSpeed(const uint8_t);
uint8_t getCustomFanSpeed();
void enableCustomFanSpeed();
void disableCustomFanSpeed();

// TODO: Remove this
extern uint16_t control_timeout;

} // namespace Fan
} // namespace SMC


#endif // __SMC_FAN__
