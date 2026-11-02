#ifndef SMC_FAN_HPP
#define SMC_FAN_HPP

#include <cstdint>

namespace SMC {
namespace Fan {

// Last temperatures read from the sensor (°C)
struct sensors_struct {
  uint8_t CPU_temperature;
  uint8_t board_temperature;
};
extern sensors_struct sensors;

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

// Fan and temperature flags
struct flags_struct {
  bool overheated;
  bool fan_override;             // SMBus 0x05: use the speed from 0x06
  bool temperature_read_failed;  // Never read
};
extern flags_struct flags;

void    init();
void    update_fan_temp();
void    powerOff();
uint8_t getFanSpeed();
void    setCustomSpeed(const uint8_t);
// Raise the fan to at least this speed on the next tick (P2L, SMBus 0x05 off)
void    raiseSpeedTo(const uint8_t speed);
uint8_t getCustomFanSpeed();

// Temperature thresholds (°C) of the fan control, adjustable from the web
// interface. They can only be lowered, between min_threshold and the
// firmware's default. Kept until the Pico restarts (Settings can save them), not
// reset at power off.
constexpr uint8_t max_thresholds = 10;
constexpr uint8_t min_threshold = 30;
uint8_t           thresholdCount();
const char*       thresholdLabel(uint8_t index);
uint8_t           thresholdDefault(uint8_t index);
uint8_t           getThreshold(uint8_t index);
// False (and nothing changed) when index or value is out of range
bool setThreshold(uint8_t index, uint8_t value);
void resetThresholds();

}  // namespace Fan
}  // namespace SMC

#endif  // SMC_FAN_HPP
