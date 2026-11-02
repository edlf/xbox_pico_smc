#include "smc_fan.hpp"
#include "smc_fan_internal.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include <string>

namespace SMC {
namespace Fan {

flags_struct   flags;
sensors_struct sensors;

// Fan speed and CPU temperature prediction, shared by the PIC (smc_fan_pic.cpp)
// and P2L (smc_fan_p2l.cpp) fan control

uint8_t fan_speed;
uint8_t custom_fan_speed;
uint8_t cpu_temperature_increase;
uint8_t cpu_temperature_predicted;

static uint8_t          previous_cpu_temperature;
static volatile uint8_t raise_to_speed;  // P2L: SMBus 0x05 off, applied on the next tick

void initShared(const uint8_t custom_speed) {
  fan_speed = 0;
  custom_fan_speed = custom_speed;
  raise_to_speed = 0;
  previous_cpu_temperature = 0x17;
  cpu_temperature_increase = 0;
  cpu_temperature_predicted = 0x17;
}

void setSpeed(const uint8_t speed) {
  const uint8_t previous = fan_speed;  // 0: just powered on

  // Like the PIC's set_fan_speed (0xE85) anything above 50 becomes 50, and
  // unlike it the fan is never stopped: anything below 10 becomes 10. This
  // covers the custom speed from SMBus 0x06 and the web interface too.
  if (speed < min_fan_speed) {
    fan_speed = min_fan_speed;
  } else if (speed > max_fan_speed) {
    fan_speed = max_fan_speed;
  } else {
    fan_speed = speed;
  }

  if (fan_speed != previous) {
    std::string msg = "FAN: Speed ";
    if (previous != 0) {
      msg += std::to_string(previous) + " -> ";
    }
    msg += std::to_string(fan_speed) + " (CPU " + std::to_string(sensors.CPU_temperature) + " C";
    if (!SMC_FIRMWARE_P2L) {  // P2L has no board sensor
      msg += ", board " + std::to_string(sensors.board_temperature) + " C";
    }
    msg += flags.fan_override ? ", manual)" : ")";
    debug::print_message(msg);
  }
  hal::set_fan_speed(fan_speed);
}

void applyRaisedSpeed() {
  if (raise_to_speed != 0) {
    if (fan_speed < raise_to_speed) {
      setSpeed(raise_to_speed);
    }
    raise_to_speed = 0;
  }
}

void predictCpuTemperature() {
  // Same as the PIC (0xE33): add a margin based on how fast the CPU heats up
  if (sensors.CPU_temperature >= previous_cpu_temperature) {
    cpu_temperature_increase = sensors.CPU_temperature - previous_cpu_temperature;
  } else {
    cpu_temperature_increase = 0;
  }

  uint8_t margin;
  switch (cpu_temperature_increase) {
    case 0:
      margin = 0;
      break;
    case 1:
      margin = 2;
      break;
    case 2:
      margin = 8;
      break;
    case 3:
      margin = 0x12;
      break;
    default:
      margin = 0x19;
      break;
  }

  const uint16_t predicted = sensors.CPU_temperature + margin;
  cpu_temperature_predicted = (predicted > 0x7F) ? 0x7F : predicted;
  previous_cpu_temperature = sensors.CPU_temperature;
}

void printReadings() {
  debug::print_debug("FAN: CPU " + std::to_string(sensors.CPU_temperature) + " board " + std::to_string(sensors.board_temperature) + " predicted " +
                       std::to_string(cpu_temperature_predicted) + " speed " + std::to_string(fan_speed));
}

// Called by the main loop once the Xbox is off (0x279)
void powerOff() {
  fan_speed = 0;  // No power to the fan
  hal::fan_pin_low();
}

uint8_t getFanSpeed() {
  return fan_speed;
}

void raiseSpeedTo(const uint8_t speed) {
  raise_to_speed = speed;
}

void setCustomSpeed(const uint8_t in) {
  custom_fan_speed = in;
}

uint8_t getCustomFanSpeed() {
  return custom_fan_speed;
}

uint8_t thresholdCount() {
  uint8_t count;
  thresholdEntries(count);
  return count;
}

static const threshold_entry* thresholdEntry(const uint8_t index) {
  uint8_t                count;
  const threshold_entry* entries = thresholdEntries(count);
  return index < count ? &entries[index] : nullptr;
}

const char* thresholdLabel(const uint8_t index) {
  const threshold_entry* entry = thresholdEntry(index);
  return entry != nullptr ? entry->label : "";
}

uint8_t thresholdDefault(const uint8_t index) {
  const threshold_entry* entry = thresholdEntry(index);
  return entry != nullptr ? entry->default_value : 0;
}

uint8_t getThreshold(const uint8_t index) {
  const threshold_entry* entry = thresholdEntry(index);
  return entry != nullptr ? *entry->value : 0;
}

bool setThreshold(const uint8_t index, const uint8_t value) {
  const threshold_entry* entry = thresholdEntry(index);
  // Never above the firmware's default: lower only makes the fan react sooner
  // or the Xbox shut down earlier
  if (entry == nullptr || value < min_threshold || value > entry->default_value) {
    return false;
  }
  *entry->value = value;
  return true;
}

void resetThresholds() {
  uint8_t                count;
  const threshold_entry* entries = thresholdEntries(count);
  for (uint8_t i = 0; i < count; i++) {
    *entries[i].value = entries[i].default_value;
  }
}

}  // namespace Fan
}  // namespace SMC
