#include "smc_fan.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "smc_smbus.hpp"

namespace SMC {
namespace Fan {

// Same as the PIC's fan/temperature control (0x36F). Speeds are 10-50 (the PIC
// also has 0, fan stopped) and every count is in 20 ms ticks.
constexpr uint8_t min_fan_speed = hal::FAN_MIN_SPEED;
constexpr uint8_t max_fan_speed = hal::FAN_MAX_SPEED;
constexpr uint8_t start_fan_speed = 30;

// Thresholds in °C, hex as in the PIC
constexpr uint8_t critical_cpu_temp = 0x58;      // 88: overheated
constexpr uint8_t critical_board_temp = 0x4B;    // 75
#if SMC_FIRMWARE_P11
// P11 runs the fan faster: these three are 7 °C lower than P01
constexpr uint8_t hot_cpu_temp = 0x43;           // 67: +3 speed
constexpr uint8_t warm_predicted_temp = 0x43;    // 67: +1 speed
constexpr uint8_t cool_predicted_temp = 0x42;    // 66: below and steady, maybe -1
#else
constexpr uint8_t hot_cpu_temp = 0x4A;           // 74: +3 speed
constexpr uint8_t warm_predicted_temp = 0x4A;    // 74: +1 speed
constexpr uint8_t cool_predicted_temp = 0x49;    // 73: below and steady, maybe -1
#endif
constexpr uint8_t hot_predicted_temp = 0x56;     // 86: +3 speed
constexpr uint8_t warm_board_temp = 0x3C;        // 60: +1 speed
constexpr uint8_t cool_board_temp = 0x39;        // 57
constexpr uint8_t shutdown_cpu_temp = 0x61;      // 97: still overheated, give up
constexpr uint8_t shutdown_board_temp = 0x54;    // 84

#if SMC_FIRMWARE_P2L
// P2L (0xECD) only uses the CPU temperature, all limits are "at least"
constexpr uint8_t p2l_overheat_cpu_temp = 0x59;  // 89: overheated
constexpr uint8_t p2l_hot_cpu_temp = 0x4E;       // 78: +3 speed
constexpr uint8_t p2l_hot_predicted_temp = 0x57; // 87: +3 speed
constexpr uint8_t p2l_warm_predicted_temp = 0x4E; // 78: +1 speed
constexpr uint8_t p2l_hold_predicted_temp = 0x4C; // 76: hold, below and steady -1
constexpr uint8_t p2l_shutdown_cpu_temp = 0x62;  // 98: still overheated, give up
#endif

// Faked readings when the sensor can't be read, forcing an overheat shutdown
constexpr uint8_t failed_read_cpu_temp = 0x59;
constexpr uint8_t failed_read_board_temp = 0x4C;

#if SMC_FIRMWARE_P2L
constexpr uint8_t sample_count = 128; // P2L (0x170E): average of 128 readings, one per tick
// Next decision 4 x 250 ticks (20 s) after the last one, sampling included
constexpr uint8_t wait_round_ticks = 0xFA;
constexpr uint8_t wait_rounds = 4;
#else
constexpr uint8_t sample_count = 3;   // Median of 3 readings on 3 ticks
// Next decision in 20 x 50 ticks (20 s)
constexpr uint8_t wait_round_ticks = 0x32;
constexpr uint8_t wait_rounds = 0x14;
#endif
constexpr uint8_t read_retries = 20;  // Ticks to retry a failed reading
// I2C bus recovery before giving up on the sensor; DXF and P2L don't have it
constexpr bool bus_recovery = !SMC_FIRMWARE_DXF && !SMC_FIRMWARE_P2L;

uint8_t fan_speed;
uint8_t custom_fan_speed;
fan_control_state state;
uint8_t timeout1; // 0x47
uint8_t timeout2; // 0x46

uint8_t samples_left;  // 0x59
uint8_t retry_timeout; // 0x5A
#if SMC_FIRMWARE_P2L
uint16_t cpu_sum;
uint16_t board_sum;
#else
uint8_t cpu_samples[sample_count];
uint8_t board_samples[sample_count];
#endif
bool bus_recovery_allowed; // 0xBD.1, re-armed by every good reading
volatile uint8_t raise_to_speed; // P2L: SMBus 0x05 off, applied on the next tick

uint8_t previous_cpu_temperature;
uint8_t cpu_temperature_increase;
uint8_t cpu_temperature_predicted;

void init() {
  // Same as the PIC's RAM_init (0xB58)
  state = fan_control_state::initial;
  fan_speed = 0;
#if SMC_FIRMWARE_P2L
  custom_fan_speed = 0x32; // 50, so SMBus 0x05 works on its own (0x3FE)
#else
  custom_fan_speed = 0x33; // > 50, so SMBus 0x05 can't enable it before 0x06
#endif
  timeout1 = 0;
  timeout2 = 0;
  samples_left = 0;
  retry_timeout = 0;
#if SMC_FIRMWARE_P2L
  cpu_sum = 0;
  board_sum = 0;
#else
  for (uint8_t i = 0; i < sample_count; i++) {
    cpu_samples[i] = 0;
    board_samples[i] = 0;
  }
#endif
  bus_recovery_allowed = true;
  raise_to_speed = 0;
  previous_cpu_temperature = 0x17;
  cpu_temperature_increase = 0;
  cpu_temperature_predicted = 0x17;
}

static void setSpeed(uint8_t speed) {
  // Like the PIC's set_fan_speed (0xE85) anything above 50 becomes 50, and
  // unlike it the fan is never stopped: anything below 10 becomes 10. This
  // covers the custom speed from SMBus 0x06 and the web interface too.
  if (speed < min_fan_speed) {
    speed = min_fan_speed;
  } else if (speed > max_fan_speed) {
    speed = max_fan_speed;
  }
  fan_speed = speed;
  hal::set_fan_speed(fan_speed);
}

// Called by the main loop once the Xbox is off (0x279)
void powerOff() {
  fan_speed = 0; // No power to the fan
  hal::fan_pin_low();
}

#if !SMC_FIRMWARE_P2L
static uint8_t median(uint8_t* samples) {
  // Sort of 3 values, same result as the PIC's bubble sort (0xB05)
  for (uint8_t pass = 0; pass < sample_count - 1; pass++) {
    for (uint8_t i = 0; i < sample_count - 1 - pass; i++) {
      if (samples[i + 1] < samples[i]) {
        const uint8_t t = samples[i];
        samples[i] = samples[i + 1];
        samples[i + 1] = t;
      }
    }
  }
  return samples[1];
}
#endif

static void predictCpuTemperature() {
  // Same as the PIC (0xE33): add a margin based on how fast the CPU heats up
  if (sensors.CPU_temperature >= previous_cpu_temperature) {
    cpu_temperature_increase = sensors.CPU_temperature - previous_cpu_temperature;
  } else {
    cpu_temperature_increase = 0;
  }

  uint8_t margin;
  switch (cpu_temperature_increase) {
  case 0:  margin = 0;    break;
  case 1:  margin = 2;    break;
  case 2:  margin = 8;    break;
  case 3:  margin = 0x12; break;
  default: margin = 0x19; break;
  }

  const uint16_t predicted = sensors.CPU_temperature + margin;
  cpu_temperature_predicted = (predicted > 0x7F) ? 0x7F : predicted;
  previous_cpu_temperature = sensors.CPU_temperature;
}

void update_fan_temp() {
  // P2L: SMBus 0x05 turned the override off (from the SMBus interrupt)
  if (raise_to_speed != 0) {
    if (fan_speed < raise_to_speed) {
      setSpeed(raise_to_speed);
    }
    raise_to_speed = 0;
  }

  switch (state) {
  case fan_control_state::initial:
#if !SMC_FIRMWARE_P2L
    setSpeed(min_fan_speed); // 0 on the PIC; P2L keeps the 30 from power on
#endif
    flags.overheated = false;
    if (!flags.prepare_for_shutdown) {
      state = fan_control_state::start;
    }
    break;

  case fan_control_state::start:
    setSpeed(start_fan_speed);
    timeout2 = 5;
    state = fan_control_state::start_delay;
    break;

  case fan_control_state::decision_state:
    timeout2 = wait_rounds;
#if SMC_FIRMWARE_P2L
    // P2L counts the sampling ticks in the first round
    timeout1 = wait_round_ticks - sample_count;

    if (sensors.CPU_temperature >= p2l_overheat_cpu_temp) {
      state = fan_control_state::overheated;
    } else if (flags.fan_override) {
      state = fan_control_state::custom_fan_speed;
    } else if (sensors.CPU_temperature >= p2l_hot_cpu_temp || cpu_temperature_predicted >= p2l_hot_predicted_temp) {
      state = fan_control_state::cpu_hot;
    } else if (cpu_temperature_predicted >= p2l_warm_predicted_temp) {
      state = fan_control_state::board_hot;
    } else if (cpu_temperature_predicted >= p2l_hold_predicted_temp || cpu_temperature_increase != 0) {
      state = fan_control_state::wait;
    } else {
      state = fan_control_state::board_cool;
    }
    break;
#else
    timeout1 = wait_round_ticks; // Next decision in 20 * 50 ticks = 20 s
#endif

    if (sensors.CPU_temperature > critical_cpu_temp || sensors.board_temperature > critical_board_temp) {
      state = fan_control_state::overheated;
    } else if (flags.fan_override) {
      state = fan_control_state::custom_fan_speed;
    } else if (sensors.CPU_temperature > hot_cpu_temp || cpu_temperature_predicted > hot_predicted_temp) {
      state = fan_control_state::cpu_hot;
    } else if (cpu_temperature_predicted > warm_predicted_temp || sensors.board_temperature > warm_board_temp) {
      state = fan_control_state::board_hot;
    } else if (cpu_temperature_predicted < cool_predicted_temp && cpu_temperature_increase == 0 &&
               sensors.board_temperature < cool_board_temp) {
      state = fan_control_state::board_cool;
    } else {
      state = fan_control_state::wait;
    }
    break;

  case fan_control_state::overheated:
    debug::print_warn("FAN: Overheated, CPU " + std::to_string(sensors.CPU_temperature) + " board " +
                      std::to_string(sensors.board_temperature));
    setSpeed(max_fan_speed);
    flags.cold_reset_request = true;
    flags.run_after_reset = false;
    flags.overheated = true; // SMI shuts the Xbox down once this clears
#if SMC_FIRMWARE_P2L
    flags.audio_clamp_request = true; // P2L (0xF4A) also mutes the audio
#endif
    timeout1 = 0xFA;
    timeout2 = 0xB3;
    state = fan_control_state::overheated_cooldown;
    break;

  case fan_control_state::overheated_cooldown:
    if (--timeout1 != 0) {
      return;
    }
#if SMC_FIRMWARE_P2L
    // P2L (0xF59) only stops early on a sensor error or at 98 °C: it checks
    // for "cooled down" but ignores the result, so it cools for the full time
    if (!read_temperatures() || sensors.CPU_temperature >= p2l_shutdown_cpu_temp) {
      flags.overheated = false;
    } else {
      state = fan_control_state::overheated_wait;
    }
    break;
#endif
    read_temperatures();
    if (sensors.CPU_temperature > shutdown_cpu_temp || sensors.board_temperature > shutdown_board_temp) {
      /* Still far too hot, shut down now */
      flags.overheated = false;
    } else if (sensors.CPU_temperature >= cool_predicted_temp || sensors.board_temperature >= cool_board_temp) {
      /* Still warm, keep cooling */
      state = fan_control_state::overheated_wait;
    } else {
      /* Cooled down, shut down */
      flags.overheated = false;
    }
    break;

  case fan_control_state::overheated_wait:
    if (--timeout2 != 0) {
      timeout1 = 0xFA;
      state = fan_control_state::overheated_cooldown;
    } else {
      flags.overheated = false;
    }
    break;

  case fan_control_state::custom_fan_speed:
    setSpeed(custom_fan_speed);
    state = fan_control_state::wait;
    break;

  case fan_control_state::board_cool:
    setSpeed(fan_speed - 1 < min_fan_speed ? min_fan_speed : fan_speed - 1);
    state = fan_control_state::wait;
    break;

  case fan_control_state::board_hot:
    if (fan_speed != max_fan_speed) {
      setSpeed(fan_speed + 1);
    }
    state = fan_control_state::wait;
    break;

  case fan_control_state::wait:
    if (flags.fan_override) {
      setSpeed(custom_fan_speed);
    }
    if (--timeout1 != 0) {
      return;
    }
    if (--timeout2 != 0) {
      timeout1 = wait_round_ticks;
    } else {
      state = fan_control_state::start_sampling;
    }
    break;

  case fan_control_state::start_delay:
    if (--timeout2 != 0) {
      return;
    }
    setSpeed(min_fan_speed);
    state = fan_control_state::start_sampling;
    break;

  case fan_control_state::sample_temperatures:
    if (!read_temperatures()) {
      if (--retry_timeout != 0) {
        return;
      }
      if (bus_recovery_allowed && bus_recovery) {
        /* Try once to free the bus, then retry */
        bus_recovery_allowed = false;
        hal::i2c_bus_recovery();
        retry_timeout = read_retries;
        return;
      }
      /* Sensor is gone: treat it as an overheat */
      debug::print_warn("FAN: Temperature sensor not responding, forcing overheat");
      sensors.CPU_temperature = failed_read_cpu_temp;
      sensors.board_temperature = failed_read_board_temp;
      state = fan_control_state::overheated;
      return;
    }

    bus_recovery_allowed = true;
#if SMC_FIRMWARE_P2L
    cpu_sum += sensors.CPU_temperature;
    board_sum += sensors.board_temperature;
#else
    cpu_samples[samples_left - 1] = sensors.CPU_temperature;
    board_samples[samples_left - 1] = sensors.board_temperature;
#endif
    retry_timeout = read_retries;
    if (--samples_left != 0) {
      return;
    }

#if SMC_FIRMWARE_P2L
    sensors.CPU_temperature = cpu_sum / sample_count;
    sensors.board_temperature = board_sum / sample_count; // Not used for decisions
#else
    sensors.CPU_temperature = median(cpu_samples);
    sensors.board_temperature = median(board_samples);
#endif
    predictCpuTemperature();
    debug::print_message("FAN: CPU " + std::to_string(sensors.CPU_temperature) + " board " +
                         std::to_string(sensors.board_temperature) + " predicted " +
                         std::to_string(cpu_temperature_predicted) + " speed " + std::to_string(fan_speed));
    state = fan_control_state::decision_state;
    break;

  case fan_control_state::cpu_hot:
    if (fan_speed != max_fan_speed) {
      setSpeed(fan_speed + 3);
    }
    state = fan_control_state::wait;
    break;

  case fan_control_state::start_sampling:
    samples_left = sample_count;
    retry_timeout = read_retries;
#if SMC_FIRMWARE_P2L
    cpu_sum = 0;
    board_sum = 0;
#endif
    state = fan_control_state::sample_temperatures;
    break;

  default:
    state = fan_control_state::initial;
    break;
  }
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

} // namespace Fan
} // namespace SMC
