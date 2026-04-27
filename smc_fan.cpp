#include "smc_fan.hpp"
#include "smc.hpp"
#include "pico_hal.hpp"
#include "smc_smbus.hpp"

namespace SMC {
namespace Fan {

uint8_t cpu_temperature_history[temperature_history_length];
uint8_t board_temperature_history[temperature_history_length];

void update_fan_temp() {
  static uint8_t temperature_history_counter = 0;
  static uint8_t temperature_sample_rate = 0;

  switch (state.fan_control) {
  case fan_control_state::initial:
    sensors.fan_speed = 0;
    set_fan_speed();
    clearStatusBit(overheated);
    if (!checkStatusBit(prepare_for_shutdown)) {
      state.fan_control = fan_control_state::state1;
    }
    break;

  case fan_control_state::state1:
    sensors.fan_speed = 30;
    set_fan_speed();
    timers.fan_control_timeout2 = 5;
    state.fan_control = fan_control_state::state10;
    break;

  case fan_control_state::decision_state:
    timers.fan_control_timeout2 = 14;
    timers.fan_control_timeout1 = 32;

    /* Check if CPU temp > 88 or board temp > 75 (critical) */
    if (sensors.CPU_temperature > 88 || sensors.board_temperature > 75) {
      state.fan_control = fan_control_state::overheated;
    } else if (flags.bitfield_DATA_6F & 0x02) {
      /* Custom fan speed requested */
      state.fan_control = fan_control_state::custom_fan_speed;
    } else if (sensors.CPU_temperature > 74 || sensors.CPU_temp_predicted > 86) {
      /* CPU is warming up */
      state.fan_control = fan_control_state::board_hot;
    } else if (sensors.CPU_temp_predicted > 74) {
      /* Predicted temp is in caution range */
      state.fan_control = fan_control_state::board_hot;
    } else if (sensors.board_temperature > 60) {
      /* Board getting warm */
      state.fan_control = fan_control_state::board_hot;
    } else if (sensors.CPU_temp_predicted > 73) {
      /* Check CPU temp increase trend */
      if (!something_with_cpu_temp) { // CPU not heating up
        state.fan_control = fan_control_state::state9;
      } else {
        /* Board temperature check */
        if (sensors.board_temperature < 57) {
          state.fan_control = fan_control_state::board_cool;
        } else {
          state.fan_control = fan_control_state::state9;
        }
      }
    } else {
      state.fan_control = fan_control_state::state9; // Normal
    }
    break;

  case fan_control_state::overheated:
    sensors.fan_speed = 50;
    set_fan_speed();
    flags.bitfield_DATA_70 |= 0x01; // System error flag
    flags.bitfield_DATA_70 &= ~0x02;
    setStatusBit(overheated);
    timers.fan_control_timeout1 = 0xFA;
    timers.fan_control_timeout2 = 0xB3;
    state.fan_control = fan_control_state::overheated_cooldown;
    break;

  case fan_control_state::overheated_cooldown:
    /* Waiting for overheated timeout to expire before reading temps */
    if (--timers.fan_control_timeout1 == 0) {
      read_temperatures();
      /* Check if temperature decreased enough to exit overheated state */
      if (sensors.CPU_temperature < 97 && sensors.board_temperature < 84) {
        if (sensors.CPU_temperature < 49 && sensors.board_temperature < 39) {
          state.fan_control = fan_control_state::state5;
        } else {
          clearStatusBit(overheated);
        }
      }
    }
    break;

  case fan_control_state::state5:
    /* Temperature decremented - check for further cooling */
    if (--timers.fan_control_timeout2 == 0) {
      pico_hal::set_fan_on();
      timers.fan_control_timeout1 = 0xFA;
      state.fan_control = fan_control_state::overheated_cooldown;
    } else {
      pico_hal::set_fan_on();
      state.fan_control = fan_control_state::overheated_cooldown;
    }
    break;

  case fan_control_state::custom_fan_speed:
    sensors.fan_speed = config.custom_fan_speed;
    set_fan_speed();
    state.fan_control = fan_control_state::state9;
    break;

  case fan_control_state::board_cool:
    sensors.fan_speed--;
    if (sensors.fan_speed < 10) {
      sensors.fan_speed = 10; // Minimum fan speed
    }
    set_fan_speed();
    state.fan_control = fan_control_state::state9;
    break;

  case fan_control_state::board_hot:
    if (sensors.fan_speed != 50) {
      sensors.fan_speed++;
    }
    set_fan_speed();
    state.fan_control = fan_control_state::state9;
    break;

  case fan_control_state::state9:
    /* Check for custom fan speed timeout */
    if (flags.bitfield_DATA_6F & 0x02) {
      sensors.fan_speed = config.custom_fan_speed;
      set_fan_speed();
    }
    /* Decrement main timeout */
    if (--timers.fan_control_timeout1 == 0) {
      if (--timers.fan_control_timeout2 == 0) {
        state.fan_control = fan_control_state::state13; // Go to sample state
      } else {
        timers.fan_control_timeout1 = 32; // Reset
        state.fan_control = fan_control_state::overheated_cooldown;
      }
    }
    break;

  case fan_control_state::state10:
    /* Initial fan speed delay */
    if (--timers.fan_control_timeout2 == 0) {
      sensors.fan_speed = 10;
      set_fan_speed();
      state.fan_control = fan_control_state::state13;
    }
    break;

  case fan_control_state::state11:
    /* Read and sample temperatures */
    read_temperatures();
    if ((flags.bitfield_DATA_74 & 0x01) == 0) {
      /* First time - initialize temperature history */
      something_with_cpu_temp = true;

      if (temperature_history_counter >= temperature_history_length) {
        temperature_history_counter = 0;
      }

      cpu_temperature_history[temperature_history_counter] = sensors.CPU_temperature;
      board_temperature_history[temperature_history_counter] = sensors.board_temperature;

      timers.fan_control_timeout1 = 14;
      if (++temperature_history_counter != 0) {
        return;
      }
    } else {
      /* Retrieve and process temperature history */
      if (!something_with_cpu_temp) {
        state.fan_control = fan_control_state::decision_state;
        return;
      }
      something_with_cpu_temp != something_with_cpu_temp;

      busy_wait_ms(2); // Short delay
      // TODO call predict_CPU_temperature()

      timers.fan_control_timeout1 = 14;
      state.fan_control = fan_control_state::decision_state;
    }
    break;

  case fan_control_state::cpu_hot:
    state.fan_control = fan_control_state::overheated_cooldown;
    break;

  case fan_control_state::state13:
    /* Sample rate countdown before reading temperatures */
    if (--timers.fan_control_timeout2 == 0) {
      sensors.fan_speed = 10;
      set_fan_speed();
      state.fan_control = fan_control_state::state13; // Stay in this state
    }
    break;

  default:
    state.fan_control = fan_control_state::initial;
    break;
  }
}

void set_fan_speed() {
  // Done, untested
  if (sensors.fan_speed > 50) {
    sensors.fan_speed = 50;
  }

  if (sensors.fan_speed == 0) {
    pico_hal::set_fan_off();
  } else {
    pico_hal::set_fan_pwm(sensors.fan_speed);
    pico_hal::set_fan_on();
  }
}

} // namespace Fan
} // namespace SMC
