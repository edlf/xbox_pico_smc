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

// How quickly the fan follows the fan control (saved settings). The control
// decides every 20 s; up multiplies its steps up (warm +1, hot +3), down is
// its step down when cool: 1-5, 1 like the PIC. The glide is ms per speed
// step while the fan speeds up or slows down, 0-1000: 0 (the default) changes
// at once, like the PIC. An overheat always goes straight to full speed.
constexpr uint8_t  max_ramp_step = 5;
constexpr uint16_t max_glide_ms = 1000;
struct Ramp {
  uint8_t  up;
  uint8_t  down;
  uint16_t glide_up_ms;
  uint16_t glide_down_ms;
};
constexpr Ramp default_ramp = {1, 1, 0, 0};
const Ramp&    ramp();
bool           setRamp(const Ramp& ramp);  // False (and unchanged) if out of range
// Bypass for a bad ADM1032 (saved setting, off by default): the CPU
// temperature comes from the NTC thermistor and the board temperature from
// the Pico's own sensor, and the ADM1032 isn't touched. Only with the NTC on
// and connected: without an NTC reading the temperature read fails, which the
// fan control treats as a dead sensor (an overheat), as it would the ADM1032.
bool temperatureBypass();
void setTemperatureBypass(bool on);
// Added to the bypass's readings, in whole °C (saved, +10 by default): cpu to
// the NTC's, board to the Pico's. Only for what the fan control and SMBus
// 0x09/0x0A see: 0x31/0x32 stay raw.
constexpr int8_t max_bypass_offset = 30;
struct BypassOffsets {
  int8_t cpu;
  int8_t board;
};
constexpr BypassOffsets default_bypass_offsets = {10, 10};
const BypassOffsets&    bypassOffsets();
bool                    setBypassOffsets(const BypassOffsets& offsets);  // False (and unchanged) outside -30 to 30

// Once per tick, after update_fan_temp(): moves the fan towards its speed
void updateGlide();
// The speed the fan is at now (gliding towards getFanSpeed())
uint8_t getOutputSpeed();

// Temperature thresholds (°C) of the fan control, adjustable from the web
// interface and the USB config. They can only be lowered, between
// min_threshold and the firmware's default. Not reset at power off; kept
// across a Pico restart only once saved (Settings).
constexpr uint8_t max_thresholds = 10;
// Not lower: with a threshold under the normal running temperature the Xbox
// sees an overheat as soon as it starts and keeps shutting down
constexpr uint8_t min_threshold = 40;
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
