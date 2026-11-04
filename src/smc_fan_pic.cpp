#include "smc_fan.hpp"
#include "smc_fan_internal.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "smc_smbus.hpp"

namespace SMC {
namespace Fan {

// Same as the PIC's fan/temperature control, used by P01, P11, D01 and
// DXF. P2L has its own in smc_fan_p2l.cpp.

// Thresholds in °C, hex as in the PIC. Above ("hot", "warm", "critical",
// "shutdown") or below ("cool") counts.
struct temperature_thresholds {
  uint8_t critical_cpu;  // Overheated
  uint8_t critical_board;
  uint8_t hot_cpu;         // +3 speed
  uint8_t hot_predicted;   // +3 speed
  uint8_t warm_predicted;  // +1 speed
  uint8_t warm_board;      // +1 speed
  uint8_t cool_predicted;  // Below and steady, maybe -1
  uint8_t cool_board;
  uint8_t shutdown_cpu;  // Still overheated after the cool-down, give up
  uint8_t shutdown_board;
};

constexpr temperature_thresholds p01_thresholds = {
    0x58,  // critical_cpu    88
    0x4B,  // critical_board  75
    0x4A,  // hot_cpu         74
    0x56,  // hot_predicted   86
    0x4A,  // warm_predicted  74
    0x3C,  // warm_board      60
    0x49,  // cool_predicted  73
    0x39,  // cool_board      57
    0x61,  // shutdown_cpu    97
    0x54,  // shutdown_board  84
};

// P11 runs the fan faster: hot_cpu, warm_predicted and cool_predicted are
// 7 °C lower than P01
constexpr temperature_thresholds p11_thresholds = {
    0x58,  // critical_cpu    88
    0x4B,  // critical_board  75
    0x43,  // hot_cpu         67
    0x56,  // hot_predicted   86
    0x43,  // warm_predicted  67
    0x3C,  // warm_board      60
    0x42,  // cool_predicted  66
    0x39,  // cool_board      57
    0x61,  // shutdown_cpu    97
    0x54,  // shutdown_board  84
};

constexpr const temperature_thresholds& default_thresholds = SMC_FIRMWARE_P11 ? p11_thresholds : p01_thresholds;

// In use, can be lowered (web interface, USB config)
static temperature_thresholds thresholds = default_thresholds;

static const threshold_entry threshold_entries[] = {
    {"CPU overheat (above)",                      default_thresholds.critical_cpu,   &thresholds.critical_cpu  },
    {"Board overheat (above)",                    default_thresholds.critical_board, &thresholds.critical_board},
    {"CPU hot, +3 speed (above)",                 default_thresholds.hot_cpu,        &thresholds.hot_cpu       },
    {"Predicted CPU hot, +3 speed (above)",       default_thresholds.hot_predicted,  &thresholds.hot_predicted },
    {"Predicted CPU warm, +1 speed (above)",      default_thresholds.warm_predicted, &thresholds.warm_predicted},
    {"Board warm, +1 speed (above)",              default_thresholds.warm_board,     &thresholds.warm_board    },
    {"Predicted CPU cool, -1 speed (below)",      default_thresholds.cool_predicted, &thresholds.cool_predicted},
    {"Board cool, -1 speed (below)",              default_thresholds.cool_board,     &thresholds.cool_board    },
    {"CPU still overheated, shut down (above)",   default_thresholds.shutdown_cpu,   &thresholds.shutdown_cpu  },
    {"Board still overheated, shut down (above)", default_thresholds.shutdown_board, &thresholds.shutdown_board},
};

const threshold_entry* thresholdEntries(uint8_t& count) {
  count = sizeof(threshold_entries) / sizeof(threshold_entries[0]);
  return threshold_entries;
}

constexpr uint8_t sample_count = 3;  // Median of 3 readings on 3 ticks
// Next decision in 20 x 50 ticks (20 s)
constexpr uint8_t wait_round_ticks = 0x32;
constexpr uint8_t wait_rounds = 0x14;

// I2C bus recovery before giving up on the sensor; DXF doesn't have it
constexpr bool bus_recovery = !SMC_FIRMWARE_DXF;

static fan_control_state state;
static uint8_t           timeout1;  // Ticks left in the current round
static uint8_t           timeout2;  // Rounds left

static uint8_t samples_left;   // Readings still to take
static uint8_t retry_timeout;  // Ticks left to retry a failed reading
static uint8_t cpu_samples[sample_count];
static uint8_t board_samples[sample_count];

static bool bus_recovery_allowed;  // Re-armed by every good reading

void init() {
  // Same as the PIC's RAM_init
  initShared(0x33);  // > 50, so SMBus 0x05 can't enable it before 0x06
  state = fan_control_state::initial;
  timeout1 = 0;
  timeout2 = 0;
  samples_left = 0;
  retry_timeout = 0;
  for (uint8_t i = 0; i < sample_count; i++) {
    cpu_samples[i] = 0;
    board_samples[i] = 0;
  }
  bus_recovery_allowed = true;
}

static uint8_t median(uint8_t* samples) {
  // Sort of 3 values, same result as the PIC's bubble sort
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

void update_fan_temp() {
  applyRaisedSpeed();

  switch (state) {
    case fan_control_state::initial:
      setSpeed(min_fan_speed);  // 0 on the PIC
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
      timeout1 = wait_round_ticks;

      if (sensors.CPU_temperature > thresholds.critical_cpu || sensors.board_temperature > thresholds.critical_board) {
        state = fan_control_state::overheated;
      } else if (flags.fan_override) {
        state = fan_control_state::custom_fan_speed;
      } else if (sensors.CPU_temperature > thresholds.hot_cpu || cpu_temperature_predicted > thresholds.hot_predicted) {
        state = fan_control_state::cpu_hot;
      } else if (cpu_temperature_predicted > thresholds.warm_predicted || sensors.board_temperature > thresholds.warm_board) {
        state = fan_control_state::board_hot;
      } else if (cpu_temperature_predicted < thresholds.cool_predicted && cpu_temperature_increase == 0 && sensors.board_temperature < thresholds.cool_board) {
        state = fan_control_state::board_cool;
      } else {
        state = fan_control_state::wait;
      }
      break;

    case fan_control_state::overheated:
      debug::print(debug::Level::Warnings, "FAN: Overheated, CPU %u board %u", sensors.CPU_temperature, sensors.board_temperature);
      setSpeed(max_fan_speed);
      PLL_Reset::flags.cold_reset_request = true;
      PLL_Reset::flags.run_after_reset = false;
      flags.overheated = true;  // SMI shuts the Xbox down once this clears
      timeout1 = 0xFA;
      timeout2 = 0xB3;
      state = fan_control_state::overheated_cooldown;
      break;

    case fan_control_state::overheated_cooldown:
      if (--timeout1 != 0) {
        return;
      }
      read_temperatures();
      if (sensors.CPU_temperature > thresholds.shutdown_cpu || sensors.board_temperature > thresholds.shutdown_board) {
        // Still far too hot, shut down now
        flags.overheated = false;
      } else if (sensors.CPU_temperature >= thresholds.cool_predicted || sensors.board_temperature >= thresholds.cool_board) {
        // Still warm, keep cooling
        state = fan_control_state::overheated_wait;
      } else {
        // Cooled down, shut down
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
          // Try once to free the bus, then retry
          bus_recovery_allowed = false;
          hal::i2c_bus_recovery();
          retry_timeout = read_retries;
          return;
        }
        // Sensor is gone: treat it as an overheat
        debug::print(debug::Level::Warnings, "FAN: Temperature sensor not responding, forcing overheat");
        sensors.CPU_temperature = failed_read_cpu_temp;
        sensors.board_temperature = failed_read_board_temp;
        state = fan_control_state::overheated;
        return;
      }

      bus_recovery_allowed = true;
      cpu_samples[samples_left - 1] = sensors.CPU_temperature;
      board_samples[samples_left - 1] = sensors.board_temperature;
      retry_timeout = read_retries;
      if (--samples_left != 0) {
        return;
      }

      sensors.CPU_temperature = median(cpu_samples);
      sensors.board_temperature = median(board_samples);
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
      state = fan_control_state::sample_temperatures;
      break;

    default:
      state = fan_control_state::initial;
      break;
  }
}

}  // namespace Fan
}  // namespace SMC
