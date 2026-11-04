#include "smc_fan.hpp"
#include "smc_fan_internal.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "smc_smbus.hpp"

#include <string>

namespace SMC {
namespace Fan {

// P2L (v1.6 Xyclops) fan/temperature control. Same states as the PIC's
// (smc_fan_pic.cpp), but it only uses the CPU temperature, averages 128
// readings, has no I2C bus recovery and cools down for the full time after an
// overheat.

// Thresholds in °C, all limits are "at least"
struct p2l_temperature_thresholds {
  uint8_t overheat_cpu;    // Overheated
  uint8_t hot_cpu;         // +3 speed
  uint8_t hot_predicted;   // +3 speed
  uint8_t warm_predicted;  // +1 speed
  uint8_t hold_predicted;  // Hold; below it and steady, -1
  uint8_t shutdown_cpu;    // Still overheated after the cool-down, give up
};

constexpr p2l_temperature_thresholds p2l_default_thresholds = {
    0x59,  // overheat_cpu    89
    0x4E,  // hot_cpu         78
    0x57,  // hot_predicted   87
    0x4E,  // warm_predicted  78
    0x4C,  // hold_predicted  76
    0x62,  // shutdown_cpu    98
};

// In use, can be lowered (web interface, USB config)
static p2l_temperature_thresholds p2l_thresholds = p2l_default_thresholds;

static const threshold_entry threshold_entries[] = {
    {"CPU overheat (at least)",                          p2l_default_thresholds.overheat_cpu,   &p2l_thresholds.overheat_cpu  },
    {"CPU hot, +3 speed (at least)",                     p2l_default_thresholds.hot_cpu,        &p2l_thresholds.hot_cpu       },
    {"Predicted CPU hot, +3 speed (at least)",           p2l_default_thresholds.hot_predicted,  &p2l_thresholds.hot_predicted },
    {"Predicted CPU warm, +1 speed (at least)",          p2l_default_thresholds.warm_predicted, &p2l_thresholds.warm_predicted},
    {"Predicted CPU hold, below it -1 speed (at least)", p2l_default_thresholds.hold_predicted, &p2l_thresholds.hold_predicted},
    {"CPU still overheated, shut down (at least)",       p2l_default_thresholds.shutdown_cpu,   &p2l_thresholds.shutdown_cpu  },
};

const threshold_entry* thresholdEntries(uint8_t& count) {
  count = sizeof(threshold_entries) / sizeof(threshold_entries[0]);
  return threshold_entries;
}

constexpr uint8_t sample_count = 128;  // Average of 128 readings, one per tick
// Next decision 4 x 250 ticks (20 s) after the last one, sampling included
constexpr uint8_t wait_round_ticks = 0xFA;
constexpr uint8_t wait_rounds = 4;

static fan_control_state state;
static uint8_t           timeout1;
static uint8_t           timeout2;

static uint8_t  samples_left;
static uint8_t  retry_timeout;
static uint16_t cpu_sum;
static uint16_t board_sum;

void init() {
  initShared(0x32);  // 50, so SMBus 0x05 works on its own
  state = fan_control_state::initial;
  timeout1 = 0;
  timeout2 = 0;
  samples_left = 0;
  retry_timeout = 0;
  cpu_sum = 0;
  board_sum = 0;
}

void update_fan_temp() {
  applyRaisedSpeed();  // SMBus 0x05 turned the override off

  switch (state) {
    case fan_control_state::initial:
      // Keeps the 30 from power on, the PIC sets 0 here
      flags.overheated = false;
      if (!SMI::flags.prepare_for_shutdown) {
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
      timeout1 = wait_round_ticks - sample_count;  // The sampling ticks count in the first round

      if (sensors.CPU_temperature >= p2l_thresholds.overheat_cpu) {
        state = fan_control_state::overheated;
      } else if (flags.fan_override) {
        state = fan_control_state::custom_fan_speed;
      } else if (sensors.CPU_temperature >= p2l_thresholds.hot_cpu || cpu_temperature_predicted >= p2l_thresholds.hot_predicted) {
        state = fan_control_state::cpu_hot;
      } else if (cpu_temperature_predicted >= p2l_thresholds.warm_predicted) {
        state = fan_control_state::board_hot;
      } else if (cpu_temperature_predicted >= p2l_thresholds.hold_predicted || cpu_temperature_increase != 0) {
        state = fan_control_state::wait;
      } else {
        state = fan_control_state::board_cool;
      }
      break;

    case fan_control_state::overheated:
      debug::print_warn("FAN: Overheated, CPU " + std::to_string(sensors.CPU_temperature) + " board " + std::to_string(sensors.board_temperature));
      setSpeed(max_fan_speed);
      PLL_Reset::flags.cold_reset_request = true;
      PLL_Reset::flags.run_after_reset = false;
      flags.overheated = true;                       // SMI shuts the Xbox down once this clears
      AudioClamp::flags.audio_clamp_request = true;  // Also mutes the audio
      timeout1 = 0xFA;
      timeout2 = 0xB3;
      state = fan_control_state::overheated_cooldown;
      break;

    case fan_control_state::overheated_cooldown:
      if (--timeout1 != 0) {
        return;
      }

      // Only stops early on a sensor error or at 98 °C: it checks for
      // "cooled down" but ignores the result, so it cools for the full time
      if (!read_temperatures() || sensors.CPU_temperature >= p2l_thresholds.shutdown_cpu) {
        flags.overheated = false;
      } else {
        state = fan_control_state::overheated_wait;
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
        // Sensor is gone (no bus recovery): treat it as an overheat
        debug::print_warn("FAN: Temperature sensor not responding, forcing overheat");
        sensors.CPU_temperature = failed_read_cpu_temp;
        sensors.board_temperature = failed_read_board_temp;
        state = fan_control_state::overheated;
        return;
      }

      cpu_sum += sensors.CPU_temperature;
      board_sum += sensors.board_temperature;
      retry_timeout = read_retries;
      if (--samples_left != 0) {
        return;
      }

      sensors.CPU_temperature = static_cast<uint8_t>(cpu_sum / sample_count);
      sensors.board_temperature = static_cast<uint8_t>(board_sum / sample_count);  // Not used for decisions
      predictCpuTemperature();
      printReadings();
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
      cpu_sum = 0;
      board_sum = 0;
      state = fan_control_state::sample_temperatures;
      break;

    default:
      state = fan_control_state::initial;
      break;
  }
}

}  // namespace Fan
}  // namespace SMC
