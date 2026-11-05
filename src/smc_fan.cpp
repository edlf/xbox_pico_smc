#include "smc_fan.hpp"
#include "smc_fan_internal.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include "text.hpp"

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

static uint8_t previous_cpu_temperature;
static Ramp    ramp_setting = default_ramp;
static uint8_t output_speed = 0;  // What the fan is at, 0: no speed set since power on
static uint8_t glide_ticks = 0;   // Since the last glide step
static bool    sensor_bypass = false;

bool temperatureBypass() {
  return sensor_bypass;
}

void setTemperatureBypass(const bool on) {
  sensor_bypass = on;
}
static uint8_t raise_to_speed;  // P2L: SMBus 0x05 off, applied on the next tick

const Ramp& ramp() {
  return ramp_setting;
}

bool setRamp(const Ramp& r) {
  if (r.up < 1 || r.up > max_ramp_step || r.down < 1 || r.down > max_ramp_step || r.glide_up_ms > max_glide_ms || r.glide_down_ms > max_glide_ms) {
    return false;
  }
  ramp_setting = r;
  return true;
}

uint8_t getOutputSpeed() {
  return output_speed;
}

static void output(const uint8_t speed) {
  output_speed = speed;
  glide_ticks = 0;
  hal::set_fan_speed(speed);
}

void updateGlide() {
  if (output_speed == 0 || output_speed == fan_speed) {
    return;
  }
  const bool     up = fan_speed > output_speed;
  const uint16_t ms = up ? ramp_setting.glide_up_ms : ramp_setting.glide_down_ms;
  if (++glide_ticks < (ms + 19) / 20) {  // One tick is 20 ms
    return;
  }
  output(static_cast<uint8_t>(up ? output_speed + 1 : output_speed - 1));
}

uint8_t rampedUp(const uint8_t steps) {
  const unsigned speed = fan_speed + static_cast<unsigned>(steps) * ramp_setting.up;
  return static_cast<uint8_t>(speed > max_fan_speed ? max_fan_speed : speed);
}

uint8_t rampedDown() {
  return static_cast<uint8_t>(fan_speed > min_fan_speed + ramp_setting.down ? fan_speed - ramp_setting.down : min_fan_speed);
}

void initShared(const uint8_t custom_speed) {
  fan_speed = 0;
  output_speed = 0;
  custom_fan_speed = custom_speed;
  raise_to_speed = 0;
  previous_cpu_temperature = 0x17;
  cpu_temperature_increase = 0;
  cpu_temperature_predicted = 0x17;
}

void setSpeed(const uint8_t speed) {
  const uint8_t previous = fan_speed;  // 0: just powered on

  // Like the PIC's set_fan_speed anything above 50 becomes 50, and
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
    Text<64> msg("FAN: Speed ");
    if (previous != 0) {
      msg.append("%u -> ", previous);
    }
    msg.append("%u (CPU %u C", fan_speed, sensors.CPU_temperature);
    if (!SMC_FIRMWARE_P2L) {  // P2L has no board sensor
      msg.append(", board %u C", sensors.board_temperature);
    }
    msg += flags.fan_override ? ", manual)" : ")";
    debug::print(debug::Level::Info, "%s", msg.c_str());
  }

  // At once the first time after power on, or without a glide that way
  const uint16_t glide_ms = fan_speed > output_speed ? ramp_setting.glide_up_ms : ramp_setting.glide_down_ms;
  if (output_speed == 0 || glide_ms == 0) {
    output(fan_speed);
  }
}

void setSpeedNow(const uint8_t speed) {
  setSpeed(speed);
  output(fan_speed);
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
  // Same as the PIC: add a margin based on how fast the CPU heats up
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
  cpu_temperature_predicted = static_cast<uint8_t>((predicted > 0x7F) ? 0x7F : predicted);
  previous_cpu_temperature = sensors.CPU_temperature;
}

void printReadings() {
  debug::print(debug::Level::Debug, "FAN: CPU %u board %u predicted %u speed %u", sensors.CPU_temperature, sensors.board_temperature, cpu_temperature_predicted,
               fan_speed);
}

// Called by the main loop once the Xbox is off
void powerOff() {
  fan_speed = 0;  // No power to the fan
  output_speed = 0;
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
