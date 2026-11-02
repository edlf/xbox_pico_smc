#ifndef SMC_FAN_INTERNAL_HPP
#define SMC_FAN_INTERNAL_HPP

// Shared by the fan control state machines: smc_fan_pic.cpp (P01/P11, D01,
// DXF) and smc_fan_p2l.cpp (P2L). CMake builds one of them, smc_fan.cpp holds
// what they have in common.

#include <cstdint>

#include "hal.hpp"

namespace SMC {
namespace Fan {

// Speeds are 10-50 (the PIC also has 0, fan stopped) and every count is in
// 20 ms ticks
constexpr uint8_t min_fan_speed = hal::FAN_MIN_SPEED;
constexpr uint8_t max_fan_speed = hal::FAN_MAX_SPEED;
constexpr uint8_t start_fan_speed = 30;

// Faked readings when the sensor can't be read, forcing an overheat shutdown
constexpr uint8_t failed_read_cpu_temp = 0x59;
constexpr uint8_t failed_read_board_temp = 0x4C;

constexpr uint8_t read_retries = 20;  // Ticks to retry a failed reading

extern uint8_t fan_speed;
extern uint8_t custom_fan_speed;
extern uint8_t cpu_temperature_increase;   // Since the previous decision
extern uint8_t cpu_temperature_predicted;  // CPU temperature plus a margin for the increase

// Resets the shared values (RAM_init), custom_speed for SMBus 0x06
void initShared(const uint8_t custom_speed);
// Sets the fan speed, limited to 10-50
void setSpeed(const uint8_t speed);
// Applies a raiseSpeedTo() request from the SMBus interrupt
void applyRaisedSpeed();
// Updates cpu_temperature_increase and cpu_temperature_predicted
void predictCpuTemperature();
// Logs the readings a decision is based on
void printReadings();

// One adjustable threshold: the value in use and the firmware's default
struct threshold_entry {
  const char* label;
  uint8_t     default_value;
  uint8_t*    value;
};
// The fan control's thresholds, implemented by smc_fan_pic.cpp/smc_fan_p2l.cpp
const threshold_entry* thresholdEntries(uint8_t& count);

}  // namespace Fan
}  // namespace SMC

#endif  // SMC_FAN_INTERNAL_HPP
