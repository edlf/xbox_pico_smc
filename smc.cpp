#include "smc.hpp"
#include "smc_types.hpp"
#include "debug.hpp"
#include "utils.hpp"

// Pico stuff
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "i2c_slave/include/i2c_slave.h"
#include "pico/stdlib.h"
#include "pico_hal.hpp"
#include "pin_assignments.hpp"

namespace SMC {

uint8_t failure_count;
uint8_t rtc_time;
uint8_t version_byte;

uint8_t raw_tray_status_filtered;

bool something_with_cpu_temp;
volatile uint8_t ram_test_response0;
volatile uint8_t ram_test_response1;
volatile uint8_t bios_response_byte0;
volatile uint8_t bios_response_byte1;

uint8_t cpu_temperature_history[temperature_history_length];
uint8_t board_temperature_history[temperature_history_length];

challenge_struct challenge;
state_struct state;
state_struct state_previous;
flags_struct flags;
sensors_struct sensors;
timers_struct timers;
config_struct config;
led_struct leds;

void resetInterruptReason() {
  flags.interrupt_reason = 0;
}

void setInterruptReason(uint8_t ir) {
  flags.interrupt_reason |= ir;
}

void clearInterruptReason(uint8_t ir) {
  flags.interrupt_reason &= ir;
}

bool checkInterruptReason(uint8_t ir) {
  return flags.interrupt_reason & ir;
}

void resetStatus() {
  flags.status = 0;
}

void setStatusBit(uint8_t status) {
  flags.status |= status;
}

void clearStatusBit(uint8_t status) {
  flags.status &= status;
}

uint8_t checkStatusBit(uint8_t status) {
  return flags.status & status;
}

void fireSystemInterrupt() {
  /* Pulse SMI signal (RA4) low to signal system interrupt */
  pico_hal::smi_pin_off();
  sleep_ms(20);
  pico_hal::smi_pin_on();
}

void configureConexantEncoder() {
  constexpr size_t len = 2;
  uint8_t config[len] = {0xBA, 0x3F};
  i2c_write_burst_blocking(i2c0, 0x8A, config, len);
}

void gpio_callback(uint gpio, uint32_t events) {
  uint32_t int_status = pico_hal::disableInterrupts();
  // bit 3 - AV mode changed
  // InterruptReason_av_mode_changed = 0b00001000,   // 0x08
  // bit 4 - AV cable unplugged
  // InterruptReason_av_unplugged = 0b00010000,      // 0x10

  switch (gpio) {
  case pins::SW_EJECT:
    debug::print_message("ISRL: eject sw\n");
    setInterruptReason(InterruptReason_eject_sw_pressed);
    break;

  case pins::POWER_OK:
    // ISR got called, but somehow power good didnt stay enabled
    if (!pico_hal::get_power_ok()) {
      // Power supply failure - shutdown sequence
      pico_hal::assertSystemReset();
      pico_hal::PLL_off();
      pico_hal::led_green_off();
      pico_hal::led_red_off();
      pico_hal::set_fan_off();
      pico_hal::audio_clamp_off();
      pico_hal::power_on_deassert();

      // Clear the POWOK interrupt flag and wait for watchdog reset
      resetInterruptReason();
      resetStatus();

      // Enter infinite loop waiting for watchdog timer to reset the system
      pico_hal::panic("POWER_OK timeout. Waiting for watchdog to reboot\n");
    }

    debug::print_message("ISR: power OK");
    break;

  case pins::SW_POWER:
    debug::print_message("ISR: power sw\n");
    setInterruptReason(InterruptReason_power_sw_pressed);
    break;

  case pins::VIDEO_MODE_0:
    debug::print_message("ISR: vm0");
    update_video_mode();
    break;

  case pins::VIDEO_MODE_1:
    debug::print_message("ISR: vm1");
    update_video_mode();
    break;

  case pins::VIDEO_MODE_2:
    debug::print_message("ISR: vm2");
    update_video_mode();
    break;

  case pins::TRAY_STATE_0:
    debug::print_message("ISR: ts0");
    setInterruptReason(InterruptReason_dvd_tray0);
    break;

  case pins::TRAY_STATE_1:
    debug::print_message("ISR: ts1");
    setInterruptReason(InterruptReason_dvd_tray1);
    break;

  case pins::TRAY_STATE_2:
    debug::print_message("ISR: ts2");
    setInterruptReason(InterruptReason_dvd_tray2);
    break;

  case pins::DVD_ACTIVE:
    debug::print_message("ISR: dvd active");
    break;

  default:
    debug::print_warn("ISR: Unknown interrupt");
    setInterruptReason(InterruptReason_unused);    
    break;
  }

  isr();
  pico_hal::reenableInterrupts(int_status);
}

void isr() {
  resetInterruptReason();
  resetStatus();
}

void wait_for_isr() {
  do {
  } while ((flags.interrupt_reason & 1) == 0);
}

void main_loop() {
  globals_init();
  pico_hal::init();

  pico_hal::led_red_on();
  pico_hal::led_green_on();

  gpio_set_irq_enabled_with_callback(pins::SW_POWER, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::SW_EJECT, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::DVD_EJECT, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::VIDEO_MODE_0, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::VIDEO_MODE_1, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::VIDEO_MODE_2, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::TRAY_STATE_0, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::TRAY_STATE_1, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::TRAY_STATE_2, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &gpio_callback);
  pico_hal::set_fan_off();
  pico_hal::audio_clamp_on();

  gpio_put(pins::RP2040_LED, 1);

  i2c_slave_init(i2c0, I2C_SLAVE_ADDRESS, &handle_SMBus_interrupt);

  /* Initialize state variables */
  state.standby_power = power_standby_state::initial;
  state.pwr_sw = power_switch_state::wait_for_button_press;
  state.eject_sw = eject_switch_state::wait_for_button_press;

  state.fan_control = fan_control_state::initial;
  state.dvd_tray = dvd_tray_state::initial;
  state.video_mode = 0;
  state.audio_clamp = audio_state::clamped;
  state.boot_challenge = boot_challenge_state::initial;
  state.leds = led_state::reset_phase_counter;
  state.smi_power = smi_power_state::initial;
  state.tray_eject = 1;

  timers.power_timeout3 = 1;

  uint32_t loop_count = 0;

  pico_hal::enableWatchdog();

  do {
    state_previous = state;
    // printf("Loop %d\n", loop_count);

    challenge.status_byte0 = challenge.status_byte0 + 1;
    challenge.status_byte1 = 22; // TODO Feed number from timer

    if (challenge.status_byte1 == 0) {
      challenge.status_byte1 = 1;
    }

    // TODO: State machine/init/fan control related
    // if ((DAT_DATA_00bd & 1) != 0) {
    //   DAT_DATA_00bd = DAT_DATA_00bd & 0xfe;
    //   clearStatusBit(eject_change_requested);
    // }

    uint8_t power_standby_state = update_power_standby();
    if ((power_standby_state & 1) != 0) {
      // Get timer + cpu temp for entropy
      challenge.status_byte2 = challenge.status_byte0 + 22; // TODO Feed number from timer
      challenge.status_byte3 = challenge.status_byte1 ^ sensors.CPU_temperature;

      while (true) {
        update_PLL_SYSRESET();
        update_boot_challenge();
        update_dvd_tray();
        update_eject_tray();
        update_video_mode();
        update_pwr_sw();
        update_eject_sw();
        update_dvd_tray_eject();
        update_dvd_tray3();
        update_audio_clamp();
        update_LEDs();
        update_fan_temp();

        power_standby_state = update_SMI_and_power();
        if ((power_standby_state & 1) != 0) {
          break;
        }

        busy_wait_ms(390);
        wait_for_isr();
        pico_hal::petWatchdog();
      }

      sensors.fan_speed = 0;
      set_fan_speed();

      state.standby_power = power_standby_state::initial;
      // jump_index_sub_code_828 = 0;
      state.fan_control = fan_control_state::initial;
      state.dvd_tray = dvd_tray_state::initial;
      state.video_mode = 0;
      state.audio_clamp = audio_state::clamped;
      // jump_index_sub_code_5AF = 0;
      state.boot_challenge = boot_challenge_state::initial;
      state.dvd_tray_3 = update_dvd_tray3::initial; // jump_index_sub_code_519
      state.leds = led_state::reset_phase_counter;
      state.smi_power = smi_power_state::initial;
      state.tray_eject = 1;
      pico_hal::petWatchdog();

      globals_init();
      timers.power_timeout3 = 25;

      do {
        wait_for_isr();
        pico_hal::petWatchdog();
        update_pwr_sw();
        timers.power_timeout3--;
      } while (timers.power_timeout3 != 0);

      timers.power_timeout3 = 0;
    }

    busy_wait_ms(390);

    debug::print_state_changes(state, state_previous);
    pico_hal::petWatchdog();
    loop_count++;
  } while (true);
}

uint8_t update_power_standby() {
  // Done, untested, some port stuff missing
  switch (state.standby_power) {
  case power_standby_state::initial: // State 0
    pico_hal::led_red_off();
    pico_hal::led_green_on();
    pico_hal::set_fan_off();
    pico_hal::timer1_init(500);
    sensors.tray_status = 0;
    sensors.vmode = 0x0A;
    state.standby_power = power_standby_state::read_av;
    break;

  case power_standby_state::read_av: // State 1
    update_video_mode();
    state.standby_power = power_standby_state::check_av_power_button;
    break;

  case power_standby_state::check_av_power_button: // State 2
    // Check power button or AVIP port kiosk power on
    update_pwr_sw();
    if ((sensors.vmode != 0x0E) && (!checkStatusBit(power_change_requested))) {
      state.standby_power = power_standby_state::check_eject_button;
    } else {
      state.standby_power = power_standby_state::turn_on_power_no_av;
    }
    break;

  case power_standby_state::turn_on_power_no_av: // State 3
    pico_hal::power_on_assert();
    pico_hal::set_fan_on();
    timers.power_timeout3 = 50;
    clearStatusBit(power_change_requested);
    state.standby_power = power_standby_state::powered_up_wait_power_ok;
    break;

  case power_standby_state::powered_up_wait_power_ok: // State 4
    pico_hal::petWatchdog();
    pico_hal::timer0_init(500);

    if (pico_hal::get_power_ok()) {
      state.standby_power = power_standby_state::powered_up;
    } else {
      if (--timers.power_timeout3 == 0) {
        pico_hal::panic("POWER_OK timeout. Waiting for watchdog to reboot\n");
      } else {
        pico_hal::timer1_wait();
      }
    }
    break;

  case power_standby_state::check_eject_button: // State 5
    update_eject_sw();
    if (checkStatusBit(eject_change_requested)) {
      state.standby_power = power_standby_state::turn_on_power_alternative;
    } else {
      state.standby_power = power_standby_state::idle;
    }
    break;

  case power_standby_state::idle: // State 6
    update_LEDs();
    pico_hal::timer1_wait();
    state.standby_power = power_standby_state::read_av; // Loop back to AV reading
    return 0;
    break;

  case power_standby_state::turn_on_power_alternative: // State 7
    flags.bitfield_DATA_73 |= 0x02;                    // Set eject flag
    pico_hal::power_on_assert();
    pico_hal::set_fan_on();
    timers.power_timeout3 = 0x32; // 50 cycles
    state.standby_power = power_standby_state::powered_up_wait_power_ok_alternative;
    break;

  case power_standby_state::powered_up_wait_power_ok_alternative:
    pico_hal::petWatchdog();
    pico_hal::timer1_init(500);

    if (pico_hal::get_power_ok()) {
      state.standby_power = power_standby_state::powered_up_alt;
    } else if (--timers.power_timeout3 == 0) {
      pico_hal::panic("POWER_OK timeout. Waiting for watchdog to reboot\n");
    } else {
      pico_hal::timer1_wait();
    }
    break;

  case power_standby_state::powered_up_alt:
    clearStatusBit(eject_change_requested);
    setInterruptReason(InterruptReason_eject_sw_pressed);
    utils::setBitNo(flags.bitfield_DATA_6D, 5);
    isr();
    state.standby_power = power_standby_state::powered_up;
    break;

  case power_standby_state::powered_up:
    utils::setBitNo(flags.bitfield_DATA_70, 1);
    utils::clearBitNo(flags.bitfield_DATA_70, 2);
    setStatusBit(first_execution);
    clearStatusBit(video_mode_changed);
    utils::clearBitNo(flags.bitfield_DATA_6F, 0);
    utils::clearBitNo(flags.bitfield_DATA_6F, 7);
    setStatusBit(audio_clamp_timer);
    state.video_mode = 0;
    state.dvd_tray = dvd_tray_state::initial;
    ram_test_response0 = 0;
    ram_test_response1 = 0;
    pico_hal::timer1_init(500);
    pico_hal::setupI2C();
    gpio_set_irq_enabled_with_callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    return 1;
    break;

  default:
    state.standby_power = power_standby_state::initial;
    break;
  }

  return 0;
}

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

uint8_t update_SMI_and_power() {
  // printf("update_SMI_and_power\n");
  // Manages power interrupts and SMI signaling when Xbox is powered up

  // State starts at 9 when powered on, and stops at 9 when powered off
  // State transitions:
  // 0 -> (1, 2, 10)
  // 1 -> (2, 7)
  // 2 -> (3, 4, 11)
  // 3 -> (6, 13)
  // 4 -> 0
  // 5 -> 3
  // 6 -> 8 -> 15 -> 7 -> 14 -> 16 -> 9
  // 9 -> (0, 17)
  // 10 -> (10, 14)
  // 11 -> (4, 5, 6, 12)
  // 12 -> 0
  // 13 -> (5, 6)
  // 17 -> 9

  switch (state.smi_power) {
  case smi_power_state::decision_state:
    /* Decision state - Check for pending interrupts */
    clearStatusBit(first_execution);
    flags.bitfield_DATA_70 |= 0x08;
    flags.bitfield_DATA_6F &= ~0x40;
    resetInterruptReason();

    if (checkStatusBit(overheated)) {
      /* System overheated */
      state.smi_power = smi_power_state::overheat_cooldown_wait;
    } else if (checkStatusBit(power_change_requested)) {
      /* Power button pressed */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_power_sw_pressed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::start_power_off;
    } else if (checkStatusBit(eject_change_requested)) {
      /* Eject button pressed */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_eject_sw_pressed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::start_power_off;
    } else if (flags.bitfield_DATA_72 & 0x01) {
      /* AV cable detected */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_power_sw_pressed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::start_power_off;
    } else if (flags.bitfield_DATA_71 & 0x04) {
      /* System reset request */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_dvd_tray1);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (checkStatusBit(dvd_tray)) {
      /* DVD tray change */
      setInterruptReason(InterruptReason_dvd_tray0);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (flags.bitfield_DATA_73 & 0x20) {
      /* Boot challenge event */
      setInterruptReason(InterruptReason_dvd_tray2);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (checkStatusBit(video_mode_changed)) {
      /* Video mode changed */
      setInterruptReason(InterruptReason_av_mode_changed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (flags.bitfield_DATA_71 & 0x80) {
      /* No AV cable */
      setInterruptReason(InterruptReason_av_unplugged);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    }
    break;

  case smi_power_state::start_power_off:
    /* Starting to power off */
    flags.bitfield_DATA_70 &= ~0x08;
    if (flags.bitfield_DATA_72 & 0x20) {
      /* FRAG set - go to different state */
      state.smi_power = smi_power_state::leds_off;
    } else {
      state.smi_power = smi_power_state::case2;
    }
    break;

  case smi_power_state::case2:
    /* Process power-off conditions */
    timers.power_timeout = 25;
    timers.power_timeout2 = 3;

    if (checkStatusBit(power_change_requested)) {
      state.smi_power = smi_power_state::case3;
    } else if (flags.bitfield_DATA_72 & 0x01) {
      state.smi_power = smi_power_state::case3;
    } else if (flags.bitfield_DATA_71 & 0x04) {
      if (flags.bitfield_DATA_72 & 0x02) {
        state.smi_power = smi_power_state::case11;
      } else {
        state.smi_power = smi_power_state::case11;
      }
    } else if (checkStatusBit(dvd_tray)) {
      state.smi_power = smi_power_state::case11;
    } else if (flags.bitfield_DATA_73 & 0x20) {
      state.smi_power = smi_power_state::case11;
    } else if (flags.bitfield_DATA_71 & 0x80) {
      state.smi_power = smi_power_state::case11;
    } else if (checkStatusBit(video_mode_changed)) {
      state.smi_power = smi_power_state::case11;
    } else {
      state.smi_power = smi_power_state::case11;
    }
    break;

  case smi_power_state::case3:
    /* Check RAM test results and proceed */
    if ((flags.bitfield_DATA_6F & 0x02) == 0) {
      if ((flags.bitfield_DATA_71 & 0x10) == 0) {
        state.smi_power = smi_power_state::request_tray_close;
      } else if ((flags.bitfield_DATA_71 & 0x20) == 0) {
        state.smi_power = smi_power_state::request_tray_close;
      } else if ((flags.bitfield_DATA_72 & 0x40) == 0) {
        state.smi_power = smi_power_state::request_tray_close;
      } else if (flags.bitfield_DATA_70 & 0x20) {
        state.smi_power = smi_power_state::case13;
      } else {
        if (--timers.power_timeout == 0) {
          state.smi_power = smi_power_state::request_tray_close;
        }
      }
    } else {
      state.smi_power = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::event_interrupt_handled:
    /* Event/interrupt handled */
    if (flags.interrupt_reason & 0x08) {
      clearStatusBit(video_mode_changed);
    }
    if (flags.interrupt_reason & 0x10) {
      flags.bitfield_DATA_71 &= ~0x80;
    }
    if (flags.interrupt_reason & 0x02) {
      clearStatusBit(dvd_tray);
    }
    if (flags.interrupt_reason & 0x40) {
      flags.bitfield_DATA_73 &= ~0x20;
    }
    if (flags.interrupt_reason & 0x04) {
      flags.bitfield_DATA_71 &= ~0x04;
    }
    flags.bitfield_DATA_70 &= ~0x10;
    flags.bitfield_DATA_71 &= ~0x08;
    state.smi_power = smi_power_state::decision_state;
    break;

  case smi_power_state::case5:
    /* Intermediate state */
    timers.power_timeout = 0xFF;
    flags.bitfield_DATA_70 &= ~0x20;
    flags.bitfield_DATA_6F |= 0x40;
    state.smi_power = smi_power_state::case3;
    break;

  case smi_power_state::request_tray_close:
    /* Request tray close and set timeout */
    setStatusBit(prepare_for_shutdown); // System shutting down
    timers.power_timeout = 25;
    state.smi_power = smi_power_state::wait_tray_close;
    break;

  case smi_power_state::leds_off:
    /* LEDs off */
    resetInterruptReason();
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 &= ~0x02;
    flags.bitfield_DATA_6F &= ~0x02;
    flags.bitfield_DATA_70 &= ~0x20;
    flags.bitfield_DATA_6F &= ~0x40;
    timers.power_timeout = 25;
    state.smi_power = smi_power_state::delay;
    break;

  case smi_power_state::wait_tray_close:
    /* Wait for tray to close */
    if (state.audio_clamp == audio_state::clamped && state.tray_eject == 1) {
      timers.power_timeout = 0xFF;
      state.smi_power = smi_power_state::case15;
    } else if (--timers.power_timeout == 0) {
      timers.power_timeout = 0xFF;
      state.smi_power = smi_power_state::case15;
    }
    break;

  case smi_power_state::initial:
    /* Initial state - system idle */
    flags.bitfield_DATA_72 &= ~0x20;
    flags.bitfield_DATA_70 &= ~0x08;
    clearStatusBit(video_mode_changed);
    flags.bitfield_DATA_71 &= ~0x80;
    clearStatusBit(dvd_tray);
    flags.bitfield_DATA_71 &= ~0x04;
    clearStatusBit(power_change_requested);
    flags.bitfield_DATA_72 &= ~0x01;

    if (flags.bitfield_DATA_73 & 0x40) {
      /* Power cycle requested */
      flags.bitfield_DATA_73 &= ~0x40;
      state.smi_power = smi_power_state::wait_state_for_initial;
    } else if ((checkStatusBit(first_execution)) == 0) {
      return 1; /* System powered off */
    }
    state.smi_power = smi_power_state::decision_state;
    clearStatusBit(prepare_for_shutdown);

    return 1;
    break;

  case smi_power_state::overheat_cooldown_wait:
    /* Wait for overheated status to end */
    if ((checkStatusBit(overheated)) == 0) {
      timers.power_timeout = 1;
      state.smi_power = smi_power_state::delay;
    }
    break;

  case smi_power_state::case11:
    /* Check for various conditions before going to state 4 or others */
    if (flags.bitfield_DATA_70 & 0x10) {
      if ((flags.bitfield_DATA_71 & 0x04) == 0) {
        state.smi_power = smi_power_state::event_interrupt_handled;
      } else {
        state.smi_power = smi_power_state::event_interrupt_handled;
      }
    } else if (flags.bitfield_DATA_70 & 0x20) {
      state.smi_power = smi_power_state::case5;
    } else if (flags.bitfield_DATA_6F & 0x02) {
      state.smi_power = smi_power_state::request_tray_close;
    } else if (flags.bitfield_DATA_71 & 0x08) {
      state.smi_power = smi_power_state::going_to_reset;
    } else {
      if (--timers.power_timeout == 0) {
        state.smi_power = smi_power_state::going_to_reset;
      }
    }
    break;

  case smi_power_state::going_to_reset:
    /* Going to reset */
    flags.bitfield_DATA_71 &= ~0x08;
    flags.bitfield_DATA_73 |= 0x01; // Set reset request
    clearStatusBit(video_mode_changed);
    flags.bitfield_DATA_71 &= ~0x80;
    clearStatusBit(dvd_tray);
    flags.bitfield_DATA_71 &= ~0x04;
    state.smi_power = smi_power_state::decision_state;
    break;

  case smi_power_state::case13:
    /* Intermediate handling state */
    if (--timers.power_timeout2 != 0) {
      state.smi_power = smi_power_state::case5;
    } else {
      state.smi_power = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::delay:
    /* Delay state */
    if (state.pll_reset == pll_sysreset_state::state1) {
      if (--timers.power_timeout == 0) {
        /* Proceed */
      }
    } else if (--timers.power_timeout == 0) {
      /* Proceed */
    }
    break;

  case smi_power_state::case15:
    /* Get tray status and wait for stable state */
    sensors.tray_status = pico_hal::get_tray_state();

    if ((sensors.tray_status == 0x00) || (sensors.tray_status == 0x40) || (sensors.tray_status == 0x60)) {
      state.smi_power = smi_power_state::leds_off;
    } else if (--timers.power_timeout == 0) {
      state.smi_power = smi_power_state::leds_off;
    }
    break;

  case smi_power_state::delayed_turning_off:
    /* Delayed turning power off */
    if (--timers.power_timeout == 0) {
      pico_hal::power_on_deassert();
      state.smi_power = smi_power_state::initial;
    }
    break;

  case smi_power_state::wait_state_for_initial:
    /* Wait state for power cycle */
    if (--timers.power_timeout == 0) {
      state.smi_power = smi_power_state::initial;
    }
    break;

  default:
    break;
  }

  return 0; /* System powered on */
}

void update_pwr_sw() {
  // Done, untested
  switch (state.pwr_sw) {
  case power_switch_state::wait_for_button_press:
    state.smi_power = smi_power_state::initial;
    if (pico_hal::power_button_pressed()) {
      state.pwr_sw = power_switch_state::debouncing;
    }
    break;

  case power_switch_state::debouncing:
    // state.smi_power = smi_power_state::overheat_cooldown_wait;
    if (pico_hal::power_button_pressed()) {
      setStatusBit(power_change_requested);
      state.pwr_sw = power_switch_state::wait_for_button_release;
    } else {
      state.pwr_sw = power_switch_state::wait_for_button_press;
    }
    break;

  case power_switch_state::wait_for_button_release:
    state.smi_power = smi_power_state::case11;
    if (!pico_hal::power_button_pressed()) {
      state.pwr_sw = power_switch_state::release_debounce;
    }
    break;

  case power_switch_state::release_debounce:
    state.smi_power = smi_power_state::going_to_reset;
    if (pico_hal::power_button_pressed()) {
      state.pwr_sw = power_switch_state::wait_for_button_release;
    } else {
      state.pwr_sw = power_switch_state::wait_for_button_press;
    }
    break;
  }
}

void update_eject_sw() {
  // Done, untested
  switch (state.eject_sw) {
  case eject_switch_state::wait_for_button_press:
    if (pico_hal::eject_button_pressed()) {
      state.eject_sw = eject_switch_state::debouncing;
    }
    break;

  case eject_switch_state::debouncing:
    if (pico_hal::eject_button_pressed()) {
      setStatusBit(power_change_requested);
      state.eject_sw = eject_switch_state::wait_for_button_release;
    } else {
      state.eject_sw = eject_switch_state::wait_for_button_press;
    }
    break;

  case eject_switch_state::wait_for_button_release:
    if (!pico_hal::eject_button_pressed()) {
      state.eject_sw = eject_switch_state::release_debounce;
    }
    break;

  case eject_switch_state::release_debounce:
    if (pico_hal::eject_button_pressed()) {
      state.eject_sw = eject_switch_state::wait_for_button_release;
    } else {
      state.eject_sw = eject_switch_state::wait_for_button_press;
    }
    break;
  }
}

void update_video_mode() {
  sensors.vmode_raw = pico_hal::get_video_mode();

  if (sensors.vmode_raw != sensors.vmode) {
    sensors.vmode = sensors.vmode_raw;
    setStatusBit(video_mode_changed); // Set video mode change flag
  }
}

void update_boot_challenge() {
  switch (state.boot_challenge) {
  case boot_challenge_state::initial:
    timers.boot_response_timeout = 12;
    boot_challenge_compute();
    state.boot_challenge = boot_challenge_state::wait_for_ram_test_result;
    break;

  case boot_challenge_state::wait_for_ram_test_result:
    /* Wait for RAM test result submission */
    if (flags.bitfield_DATA_71 & 0x10) {
      /* RAM test results submitted */
      if (flags.bitfield_DATA_71 & 0x20) {
        /* RAM test passed */
        state.boot_challenge = boot_challenge_state::ram_test_ok;
      } else {
        /* RAM test failed */
        state.boot_challenge = boot_challenge_state::ram_test_failed;
      }
    } else if (--timers.boot_response_timeout == 0) {
      state.boot_challenge = boot_challenge_state::challenge_failed_or_no_ram_test_result;
    }
    break;

  case boot_challenge_state::ram_test_ok:
    state.boot_challenge = boot_challenge_state::challenge_wait_for_reply;
    break;

  case boot_challenge_state::ram_test_failed:
    config.LED_red_manual_cycles = 15;
    config.LED_green_manual_cycles = 5;
    flags.bitfield_DATA_70 |= 0x04; // Manual LED control
    state.boot_challenge = boot_challenge_state::boot_failed;
    break;

  case boot_challenge_state::challenge_failed_or_no_ram_test_result:
    config.LED_red_manual_cycles = 10;
    config.LED_green_manual_cycles = 5;
    flags.bitfield_DATA_70 |= 0x04; // Manual LED control
    state.boot_challenge = boot_challenge_state::boot_failed;
    break;

  case boot_challenge_state::challenge_wait_for_reply:
    if (flags.bitfield_DATA_73 & 0x01) {
      /* System reset requested */
      flags.bitfield_DATA_71 &= ~0x10;
      flags.bitfield_DATA_71 &= ~0x20;
      state.boot_challenge = boot_challenge_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* System overheated - stay in this state */
      return;
    } else if ((flags.bitfield_DATA_70 & 0x01) == 0) {
      /* System not overheated */
      timers.boot_response_timeout = 2;
      state.boot_challenge = boot_challenge_state::check_response;
    }
    break;

  case boot_challenge_state::boot_failed:
    failure_count++;

    if (failure_count >= 3) {
      flags.bitfield_DATA_70 |= 0x04; // Manual LED control
      flags.bitfield_DATA_72 |= 0x20; // FRAG flag
      flags.bitfield_DATA_73 |= 0x04; // Locked state
      state.boot_challenge = boot_challenge_state::lock_up;
    } else {
      state.boot_challenge = boot_challenge_state::retry_boot;
    }
    break;

  case boot_challenge_state::lock_up:
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_71 &= ~0x10;
    flags.bitfield_DATA_71 &= ~0x20;
    flags.bitfield_DATA_72 &= ~0x01;
    flags.bitfield_DATA_72 &= ~0x02;
    return;

  case boot_challenge_state::retry_boot:
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_71 &= ~0x10;
    flags.bitfield_DATA_71 &= ~0x20;
    sensors.CPU_temperature = 0;
    sensors.board_temperature = 0;
    state.boot_challenge = boot_challenge_state::initial;
    break;

  case boot_challenge_state::check_response:
    if (flags.bitfield_DATA_73 & 0x10) {
      /* Response received */
      flags.bitfield_DATA_73 &= ~0x10;

      /* Verify both response bytes match expected values */
      if (challenge.response0 == challenge.expected0 && challenge.response1 == challenge.expected1) {
        state.boot_challenge = boot_challenge_state::challenge_passed;
      } else {
        state.boot_challenge = boot_challenge_state::challenge_failed_or_no_ram_test_result;
      }
    } else if (--timers.boot_response_timeout == 0) {
      state.boot_challenge = boot_challenge_state::challenge_failed_or_no_ram_test_result;
    }
    break;

  case boot_challenge_state::challenge_passed:
    if (flags.bitfield_DATA_73 & 0x01) {
      // System reset requested
      flags.bitfield_DATA_71 &= ~0x10;
      flags.bitfield_DATA_71 &= ~0x20;

      /* TODO: Update challenge bytes for next boot */
      state.boot_challenge = boot_challenge_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* System overheated - stay in this state */
      return;
    }
    break;

  case boot_challenge_state::reboot:
    if (--failure_count != 0) {
      state.boot_challenge = boot_challenge_state::retry_boot;
    } else {
      rtc_time = 4;
      flags.bitfield_DATA_70 |= 0x01;
      flags.bitfield_DATA_70 &= ~0x02;
      state.boot_challenge = boot_challenge_state::reboot;
    }
    break;

  default:
    break;
  }
}

void update_dvd_tray() {
  // Done, untested
  uint8_t tray_state;

  switch (state.dvd_tray) {
  case dvd_tray_state::initial:
    timers.tray_state_timer = 0x20;
    state.dvd_tray = dvd_tray_state::tick_timer;
    break;

  case dvd_tray_state::tick_timer:
    timers.tray_state_timer--;
    if (timers.tray_state_timer == 0) {
      state.dvd_tray = dvd_tray_state::state2;
    }
    break;

  case dvd_tray_state::state2:
    sensors.tray_status_raw = pico_hal::get_tray_state() & 0x70;
    raw_tray_status_filtered = sensors.tray_status_raw;
    if ((sensors.tray_status ^ sensors.tray_status_raw) != 0) {
      state.dvd_tray = dvd_tray_state::state3;
    }
    break;

  case dvd_tray_state::state3:
    tray_state = pico_hal::get_tray_state();
    sensors.tray_status_raw = tray_state & 0x70;
    if ((tray_state & 0x70) != raw_tray_status_filtered) {
      state.dvd_tray = dvd_tray_state::state2;
    } else {
      state.dvd_tray = dvd_tray_state::state4;
    }
    break;

  case dvd_tray_state::state4:
    sensors.tray_status = sensors.tray_status_raw;
    if (sensors.tray_status_raw == 0x30) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        utils::setBitNo(flags.bitfield_DATA_71, 2);
      }
    } else if (sensors.tray_status_raw == 0) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        utils::setBitNo(flags.bitfield_DATA_73, 5);
      }
    } else {
      if ((sensors.tray_status_raw != 0x60) && (sensors.tray_status_raw != 0x40)) {
        state.dvd_tray = dvd_tray_state::state2;
        utils::setBitNo(flags.bitfield_DATA_72, 4);
        return;
      }
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        setStatusBit(InterruptReason_eject_sw_pressed);
      }
    }
    state.dvd_tray = dvd_tray_state::state2;
    return;
    break;

  default:
    state.dvd_tray = dvd_tray_state::initial;
    break;
  }
}

void update_LEDs() {
  // Done, untested
  switch (state.leds) {
  case led_state::initial: // 0
    leds.state_counter = 6;

    if (checkStatusBit(overheated)) {
      state.leds = led_state::overheat;
      return;
    }

    if (utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      // AV cable missing
      state.leds = led_state::quick_green_orange;
      return;
    }

    if (utils::checkBitNo(flags.bitfield_DATA_70, 3)) {
      state.leds = led_state::off;
      return;
    }

    if (utils::checkBitNo(flags.bitfield_DATA_70, 2)) {
      state.leds = led_state::manual_control;
      return;
    }

    // DVD eject in progress?
    if (utils::checkBitNo(flags.bitfield_DATA_6F, 5)) {
      state.leds = led_state::quick_green_blink;
      return;
    }

    state.leds = led_state::solid_green;
    break;

  case led_state::overheat: // 1
    // System overheated - slow blinking
    leds.red_phases = 3;
    leds.green_phases = 3;
    state.leds = led_state::tick_update;
    break;

  case led_state::manual_control: // 2
    // Manual control mode
    leds.red_phases = leds.red_phases_manual;
    leds.green_phases = leds.green_phases_manual;
    state.leds = led_state::tick_update;
    break;

  case led_state::quick_green_blink: // 3
    // Quick green blinking
    leds.red_phases = 0;
    leds.green_phases = 5;
    state.leds = led_state::tick_update;
    break;

  case led_state::solid_green: // 4
    // Solid green
    leds.red_phases = 0;
    leds.green_phases = 0xF;
    state.leds = led_state::tick_update;
    break;

  case led_state::off: // 5
    // Off
    leds.red_phases = 0;
    leds.green_phases = 0;
    state.leds = led_state::tick_update;
    break;

  case led_state::tick_update: // 6
    // Counter tick
    leds.state_counter--;
    if (leds.state_counter != 0) {
      return;
    }
    state.leds = led_state::set_gpios;
    break;

  case led_state::set_gpios: // 7
    // Actually set leds according to patterns
    if (utils::checkBitNo(leds.red_phases, leds.state_counter)) {
      pico_hal::led_red_on();
    } else {
      pico_hal::led_red_off();
    }
    if (utils::checkBitNo(leds.green_phases, leds.state_counter)) {
      pico_hal::led_green_on();
    } else {
      pico_hal::led_green_off();
    }

    leds.state_counter--;
    if (leds.state_counter == -1) {
      state.leds = led_state::reset_phase_counter;
    } else {
      state.leds = led_state::initial;
    }
    break;

  case led_state::reset_phase_counter: // 8
    leds.state_counter = 3; // ? gets overwritten by the initial state with 6...
    state.leds = led_state::initial;
    break;

  case led_state::quick_green_orange: // 9
    leds.red_phases = 5;
    leds.green_phases = 0xF;
    state.leds = led_state::tick_update;
    break;

  default:
    state.leds = led_state::initial;
    break;
  }
}

void update_audio_clamp() {
  // Done, untested

  switch (state.audio_clamp) {
  case audio_state::clamped:
    // Audio clamped
    pico_hal::audio_clamp_on();
    utils::clearBitNo(flags.bitfield_DATA_6F, 0);
    timers.audio_clamp_timeout = 44;

    // Cable missing, keep audio clamped
    if (utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      return;
    }

    // Check if clamp off was requested
    if (utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
      if (!checkStatusBit(audio_clamp_timer)) {
        return;
      }
      state.audio_clamp = audio_state::tick_timer;
      return;
    }
    state.audio_clamp = audio_state::unclamped;
    break;

  case audio_state::tick_timer:
    // Audio clamped timer tick
    clearStatusBit(audio_clamp_timer);

    if (!utils::checkBitNo(flags.bitfield_DATA_71, 6) && !utils::checkBitNo(flags.bitfield_DATA_6F, 0) && !utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
      if (!utils::checkBitNo(flags.bitfield_DATA_6F, 7)) {
        timers.audio_clamp_timeout--;
        if (timers.audio_clamp_timeout != 0) {
          return;
        }
      }

      state.audio_clamp = audio_state::unclamped;
    } else {
      state.audio_clamp = audio_state::clamped;
    }
    break;

  case audio_state::unclamped:
    // Audio unclamped
    pico_hal::audio_clamp_off();
    utils::clearBitNo(flags.bitfield_DATA_6F, 7);
    clearStatusBit(audio_clamp_timer);

    if (checkStatusBit(prepare_for_shutdown) && !utils::checkBitNo(flags.bitfield_DATA_6F, 0) && !utils::checkBitNo(flags.bitfield_DATA_71, 6)) {
      return;
    }
    state.audio_clamp = audio_state::clamped;
    break;

  default:
    state.audio_clamp = audio_state::clamped;
    break;
  }
}

void update_PLL_SYSRESET() {
  switch (state.pll_reset) {
  case pll_sysreset_state::initial:
    pico_hal::assertSystemReset();
    flags.bitfield_DATA_70 &= ~0x01; // Clear flags
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_72 |= 0x04; // Set PLL control flags
    flags.bitfield_DATA_72 |= 0x02;
    state.pll_reset = pll_sysreset_state::state1;
    break;

  case pll_sysreset_state::state1:
    pico_hal::PLL_off();
    timers.fan_control_timeout1 = 0;

    if (flags.bitfield_DATA_70 & 0x01) {
      /* Cold reset path - go back to state 0 */
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x02) {
      /* Check PORTC.1 (POWOK signal) */
      if (pico_hal::get_power_ok()) {
        /* POWOK is high */
        state.pll_reset = pll_sysreset_state::cold_reset;
      }
    } else {
      /* Normal transition to wait state */
      return;
    }
    break;

  case pll_sysreset_state::state2:
    /* Wait state - checking for timing conditions */
    if (!pico_hal::get_power_ok()) {
      /* POWOK low - return to state 0 */
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* Reset condition detected */
      state.pll_reset = pll_sysreset_state::initial;
    } else {
      /* Decrement timer and check for completion */
      if (timers.fan_control_timeout1 == 0) {
        timers.fan_control_timeout1 = 2;
        state.pll_reset = pll_sysreset_state::cold_reset;
      }
    }
    break;

  case pll_sysreset_state::cold_reset:
    pico_hal::PLL_on();

    if (--timers.fan_control_timeout1 != 0) {
      return; // Wait for timeout
    }

    timers.fan_control_timeout1 = 1;
    configureConexantEncoder();
    pico_hal::liftSystemReset();
    flags.bitfield_DATA_73 |= 0x08; // Set initialization flag

    /* Wait for encoder communication */
    busy_wait_ms(132);

    // TODO: Send SMBus write command

    state.pll_reset = pll_sysreset_state::state8;
    break;

  case pll_sysreset_state::warm_reset_2:
    pico_hal::liftSystemReset();
    configureConexantEncoder();
    flags.bitfield_DATA_73 |= 0x08; // Set initialization flag

    /* Wait for encoder communication */
    busy_wait_ms(132); // Delay ~832 cycles

    state.pll_reset = pll_sysreset_state::state8;
    break;

  case pll_sysreset_state::warm_reset_1:
    pico_hal::assertSystemReset();
    flags.bitfield_DATA_73 &= ~0x01; // Clear reset request
    flags.bitfield_DATA_72 &= ~0x40;
    flags.bitfield_DATA_72 |= 0x04; // Maintain PLL control state
    flags.bitfield_DATA_72 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    state.pll_reset = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state6:
    /* PLL disable wait - decrement counter */
    if (--timers.fan_control_timeout1 != 0) {
      return;
    }

    pico_hal::PLL_off();
    pico_hal::assertSystemReset();
    timers.fan_control_timeout1 = 1;
    state.pll_reset = pll_sysreset_state::state7;
    break;

  case pll_sysreset_state::state7:
    /* PLL enable wait - wait before re-enabling PLL */
    if (--timers.fan_control_timeout1 != 0) {
      return;
    }

    pico_hal::PLL_on();
    state.pll_reset = pll_sysreset_state::warm_reset_2;
    break;

  case pll_sysreset_state::state8:
    /* Final state - wait for completion or reset request */
    if (!pico_hal::get_power_ok()) {
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* Something needs resetting */
      state.pll_reset = pll_sysreset_state::initial;
    } else if (flags.bitfield_DATA_73 & 0x01) {
      /* System reset requested */
      state.pll_reset = pll_sysreset_state::warm_reset_1; // Go to warm reset path
    }
    break;

  default:
    state.pll_reset = pll_sysreset_state::initial;
    break;
  }
}

void update_eject_tray() {
  // Done, not tested

  switch (state.update_eject_tray) {
  case update_eject_tray_state::initial:
    timers.eject_timeout = 5;

    // Check if eject signal is set
    if (utils::checkBitNo(flags.bitfield_DATA_72, 5) != 0) {
      state.update_eject_tray = update_eject_tray_state::tick_timer;
    }
    break;

  case update_eject_tray_state::tick_timer:
    pico_hal::dvd_eject_off();
    if (--timers.eject_timeout == 0) {
      state.update_eject_tray = update_eject_tray_state::release_eject;
    }
    break;

  case update_eject_tray_state::release_eject:
    utils::clearBitNo(flags.bitfield_DATA_72, 5);
    pico_hal::dvd_eject_on();
    state.update_eject_tray = update_eject_tray_state::initial;
    break;

  default:
    state.update_eject_tray = update_eject_tray_state::initial;
    break;
  }
}

void update_dvd_tray_eject() {
  /* Handle tray eject mechanism - manages DVD tray eject/inject operations
   * States: 0=check eject conditions, 1=wait for shutdown signal */

  switch (state.tray_eject) {
  case 0:
    /* Check if we should initiate tray eject */

    /* If tray is already at state 0x50 (fully ejected), nothing to do */
    if (sensors.tray_status == 0x50) {
      /* Tray already ejected - clear flags and return to state 1 */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x10; // Clear eject request
      state.tray_eject = 1;
      return;
    }

    /* Check if eject should be triggered */
    if ((checkStatusBit(eject_change_requested)) || // Eject button pressed
        (flags.bitfield_DATA_6F & 0x10)) {          // Eject request from elsewhere

      /* Check current tray state via PORTB bits 4-6 */
      uint8_t tray_state = pico_hal::get_tray_state();

      /* Only proceed if tray is not already in eject state (0x70) or insert
       * state (0x20) */
      if (tray_state != 0x70 && tray_state != 0x20) {
        /* Signal tray eject mechanism */
        flags.bitfield_DATA_72 |= 0x08; // Set tray eject signal
      }
    }

    /* Clear flags and transition to state 1 */
    clearStatusBit(eject_change_requested); // Clear eject button
    flags.bitfield_DATA_6F &= ~0x10;        // Clear eject request
    state.tray_eject = 1;
    break;

  case 1:
    /* Wait state - only act if system is shutting down */
    if (!checkStatusBit(prepare_for_shutdown)) {
      /* System is still powered - stay in this state */
      return;
    }

    /* System is shutting down */

    /* If tray is at state 0x30 (fully inserted), transition back to state 0
     */
    if (sensors.tray_status == 0x30) {
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x10; // Clear eject request
      state.tray_eject = 0;
      return;
    }

    /* Check eject button and internal flags */
    if (checkStatusBit(eject_change_requested)) {
      /* Eject button is being pressed */
      if ((flags.bitfield_DATA_72 & 0x02) == 0) {
        /* Set initialization flag */
        flags.bitfield_DATA_73 |= 0x02;
      }

      /* Check for FRAG condition */
      if ((flags.bitfield_DATA_73 & 0x04) == 0) {
        /* Not FRAG'd - can continue */
      } else {
        /* System is FRAG'd - set error flags */
        flags.bitfield_DATA_70 |= 0x02;
        flags.bitfield_DATA_72 |= 0x08; // Set eject signal
        clearStatusBit(eject_change_requested);
      }
    }

    /* Check if explicit eject request is set */
    if (flags.bitfield_DATA_6F & 0x10) {
      uint8_t tray_state = pico_hal::get_tray_state();

      /* Only proceed if not already in eject/insert states */
      if (tray_state != 0x70 && tray_state != 0x20) {
        flags.bitfield_DATA_72 |= 0x08; // Signal eject
      }
    }
    break;

  default:
    state.tray_eject = 0;
    break;
  }
}

void update_dvd_tray3() {
  // Done, untested
  switch (state.dvd_tray_3) {
  case update_dvd_tray3::initial:
    timers.dvd_tray_timeout = 0xFF;
    utils::clearBitNo(flags.bitfield_DATA_6F, 5);

    if (utils::checkBitNo(flags.bitfield_DATA_72, 7)) {
      state.dvd_tray_3 = update_dvd_tray3::eject;
    } else {
      if (!(sensors.tray_status == 0x70 ||  // Eject state
            sensors.tray_status == 0x10 ||  // State 1
            sensors.tray_status == 0x00 ||  // State 0
            sensors.tray_status == 0x40 ||  // State 4
            sensors.tray_status == 0x60)) { // State 6
        state.dvd_tray_3 = update_dvd_tray3::wait;
      }
    }
    break;

  case update_dvd_tray3::wait:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);
    utils::clearBitNo(flags.bitfield_DATA_72, 7);

    // Check if tray reached a stable position
    if (sensors.tray_status == 0x10 || // Stable position 1
        sensors.tray_status == 0x40 || // Stable position 4
        sensors.tray_status == 0x60) { // Stable position 6
      state.dvd_tray_3 = update_dvd_tray3::initial;
    }
    break;

  case update_dvd_tray3::eject:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);

    /* Check if we should exit this state based on eject flag */
    if (!utils::checkBitNo(flags.bitfield_DATA_73, 1)) {
      /* Eject flag not set - check specific tray positions */
      if (sensors.tray_status == 0x50) {
        /* Tray fully ejected */
        state.dvd_tray_3 = update_dvd_tray3::initial;
        utils::clearBitNo(flags.bitfield_DATA_73, 1);
        return;
      }
    }

    // Check other stable positions
    if (sensors.tray_status == 0x30 || // Fully inserted
        sensors.tray_status == 0x20 || // Position 2
        sensors.tray_status == 0x10) { // Position 1
      utils::clearBitNo(flags.bitfield_DATA_73, 1);
      state.dvd_tray_3 = update_dvd_tray3::wait;
      return;
    }

    // Check if we should wait on timeout
    if (utils::checkBitNo(flags.bitfield_DATA_72, 6)) {
      if (--timers.dvd_tray_timeout == 0) {
        state.dvd_tray_3 = update_dvd_tray3::wait;
        return;
      }
    }
    break;

  default:
    state.dvd_tray_3 = update_dvd_tray3::initial;
    break;
  }
}

// SMBus state
static bool command_received = false;
static uint8_t smbus_command = 0;
static uint8_t smbus_data = 0;
static uint8_t smbus_version_counter = 0;

uint8_t smbus_read_handler(uint8_t command) {
  uint8_t response = 0;

  switch (command) {
  case Command::FIRMWARE_REVISION: // 0x01
    // Multi-byte response: 'P', '0', '1'
    if (smbus_version_counter == 0) {
      response = 'P';
    } else if (smbus_version_counter == 1) {
      response = '0';
    } else if (smbus_version_counter == 2) {
      response = '1';
    }
    break;

  case Command::TRAY_STATE: // 0x03
    response = sensors.tray_status;
    // Set bit 0 if tray_insert flag is set
    if (flags.bitfield_DATA_6F & 0x20) {
      response |= 0x01;
    }
    break;

  case Command::VIDEO_MODE: // 0x04
    response = (sensors.vmode >> 1) & 0x07;
    break;

  case Command::CPU_TEMPERATURE: // 0x09
    response = sensors.CPU_temperature;
    break;

  case Command::AIR_TEMPERATURE: // 0x0A
    response = sensors.board_temperature;
    break;

  case Command::READ_FAN_SPEED: // 0x10
    response = sensors.fan_speed;
    break;

  case Command::INTERRUPT_REASON: // 0x11
    response = flags.interrupt_reason;
    flags.interrupt_reason = 0; // Clear after reading
    break;

  case Command::READ_RAM_TEST_RESULTS: // 0x14
    response = ram_test_response0;
    break;

  case Command::READ_RAM_TYPE: // 0x15
    response = ram_test_response1;
    break;

  case Command::LAST_REGISTER_WRITTEN: // 0x16
    response = smbus_command;
    break;

  case Command::LAST_BYTE_WRITTEN: // 0x17
    response = smbus_data;
    break;

  case Command::SCRATCH: // 0x1B
    response = config.custom_fan_speed;
    break;

  case Command::READ_ERROR_CODE:             // 0x0F
  case Command::AUDIO_CLAMP:                 // 0x0B
  case Command::DVD_TRAY_OPERATION:          // 0x0C
  case Command::OS_RESUME:                   // 0x0D
  case Command::WRITE_ERROR_CODE:            // 0x0E
  case Command::RESET:                       // 0x02
  case Command::FAN_OVERRIDE:                // 0x05
  case Command::REQUEST_FAN_SPEED:           // 0x06
  case Command::LED_OVERRIDE:                // 0x07
  case Command::LED_STATES:                  // 0x08
  case Command::WRITE_RAM_TEST_RESULTS:      // 0x12
  case Command::WRITE_RAM_TYPE:              // 0x13
  case Command::SOFTWARE_INTERRUPT:          // 0x18
  case Command::OVERRIDE_RESET_ON_TRAY_OPEN: // 0x19
  case Command::OS_READY:                    // 0x1A
  default:
    // Unsupported/write-only commands - return 0
    response = 0;
    break;
  }

  return response;
}

void smbus_write_handler(uint8_t command, uint8_t data) {
  switch (command) {
  case Command::RESET: // 0x02
    // data: 0x00 = warm reset, 0x01 = cold reset
    if (data == 0x00) {
      // Warm reset - keep power state
      state.pll_reset = pll_sysreset_state::warm_reset_1;
    } else if (data == 0x01) {
      // Cold reset - power cycle
      state.pll_reset = pll_sysreset_state::cold_reset;
    }
    break;

  case Command::VIDEO_MODE: // 0x04
    // Set video mode (0=NTSC, 1=PAL, etc)
    sensors.vmode = data;
    setStatusBit(Status::video_mode_changed);
    setInterruptReason(InterruptReason_av_mode_changed);
    break;

  case Command::FAN_OVERRIDE: // 0x05
    // data: bit 0 = enable override, bit 1-7 = unused
    if (data & 0x01) {
      // Enable fan override mode
      state.fan_control = fan_control_state::custom_fan_speed;
    } else {
      // Disable override, return to automatic control
      state.fan_control = fan_control_state::decision_state;
    }
    break;

  case Command::REQUEST_FAN_SPEED: // 0x06
    // Set requested fan speed (0-50)
    if (data <= 50) {
      sensors.fan_speed = data;
    } else {
      sensors.fan_speed = 50; // Clamp to max
    }
    break;

  case Command::LED_OVERRIDE: // 0x07
    // data: bit 0 = enable override
    if (data & 0x01) {
      // Enable LED manual control
      state.leds = led_state::manual_control;
    } else {
      // Disable override, return to automatic control
      state.leds = led_state::initial;
    }
    break;

  case Command::LED_STATES: // 0x08
    // data: LED color/state bits
    leds.green_phases_manual = (data >> 0) & 0x0F; // Lower 4 bits
    leds.red_phases_manual = (data >> 4) & 0x0F;   // Upper 4 bits
    break;

  case Command::AUDIO_CLAMP: // 0x0B
    // data: 0 = unclamp, non-zero = clamp
    if (data == 0) {
      state.audio_clamp = audio_state::unclamped;
      pico_hal::audio_clamp_off();
    } else {
      state.audio_clamp = audio_state::clamped;
      pico_hal::audio_clamp_on();
    }
    break;

  case Command::DVD_TRAY_OPERATION: // 0x0C
    // data: 0x00 = close, 0x01 = eject
    if (data == 0x00) {
      // Close tray
      pico_hal::dvd_eject_off();
    } else if (data == 0x01) {
      // Eject tray
      pico_hal::dvd_eject_on();
      setInterruptReason(InterruptReason_dvd_tray0);
    }
    break;

  case Command::OS_RESUME: // 0x0D
    // Mark system as resuming from sleep/standby
    clearStatusBit(Status::prepare_for_shutdown);
    break;

  case Command::WRITE_ERROR_CODE: // 0x0E
    // Store error code from BIOS (not used in normal operation)
    bios_response_byte0 = data;
    break;

  case Command::WRITE_RAM_TEST_RESULTS: // 0x12
    // Store RAM test results (bit 0 = pass/fail)
    ram_test_response0 = data;
    break;

  case Command::WRITE_RAM_TYPE: // 0x13
    // Store RAM type information
    ram_test_response1 = data;
    break;

  case Command::SOFTWARE_INTERRUPT: // 0x18
    // Trigger SMI interrupt signal to BIOS
    fireSystemInterrupt();
    if (data == 0x00) {
      // Clear the interrupt after delivery
      pico_hal::smi_pin_off();
    }
    break;

  case Command::OVERRIDE_RESET_ON_TRAY_OPEN: // 0x19
    // data: 0 = reset on tray open, non-zero = don't reset
    if (data == 0x00) {
      utils::clearBitNo(flags.bitfield_DATA_71, 0); // Allow reset on tray open
    } else {
      utils::setBitNo(flags.bitfield_DATA_71, 0); // Override reset on tray open
    }
    break;

  case Command::OS_READY: // 0x1A
    // Mark OS as fully booted and ready
    clearStatusBit(Status::first_execution);
    setStatusBit(Status::prepare_for_shutdown); // Clear prepare_for_shutdown
    break;

  case Command::SCRATCH: // 0x1B
    // Write to scratch register (general purpose storage)
    config.custom_fan_speed = data;
    break;

  case Command::FIRMWARE_REVISION:     // 0x01 (read-only, ignore writes)
  case Command::TRAY_STATE:            // 0x03 (read-only, ignore writes)
  case Command::CPU_TEMPERATURE:       // 0x09 (read-only, ignore writes)
  case Command::AIR_TEMPERATURE:       // 0x0A (read-only, ignore writes)
  case Command::READ_FAN_SPEED:        // 0x10 (read-only, ignore writes)
  case Command::INTERRUPT_REASON:      // 0x11 (read-only, ignore writes)
  case Command::READ_RAM_TEST_RESULTS: // 0x14 (read-only, ignore writes)
  case Command::READ_RAM_TYPE:         // 0x15 (read-only, ignore writes)
  case Command::LAST_REGISTER_WRITTEN: // 0x16 (read-only, ignore writes)
  case Command::LAST_BYTE_WRITTEN:     // 0x17 (read-only, ignore writes)
    // Read-only commands - silently ignore writes
    break;

  default:
    // Unknown command - silently ignore
    break;
  }
}

void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event) {
  uint8_t data;

  switch (event) {
  case I2C_SLAVE_RECEIVE: // master has written data
    data = i2c_read_byte_raw(i2c);

    if (!command_received) {
      // This is the command byte
      smbus_command = data;
      smbus_data = 0;
      command_received = true;
      smbus_version_counter = 0;
    } else {
      // This is a data byte (write operation)
      smbus_data = data;
      smbus_write_handler(smbus_command, smbus_data);
    }
    break;

  case I2C_SLAVE_REQUEST: // master is requesting data
    if (command_received) {
      uint8_t response = smbus_read_handler(smbus_command);

      // For FIRMWARE_REVISION, increment counter for multi-byte response
      if (smbus_command == Command::FIRMWARE_REVISION) {
        smbus_version_counter++;
        if (smbus_version_counter > 2) {
          smbus_version_counter = 0;
        }
      }

      i2c_write_byte_raw(i2c, response);
    }
    break;

  case I2C_SLAVE_FINISH: // master has sent Stop or Restart
    command_received = false;
    smbus_version_counter = 0;
    break;

  default:
    break;
  }
}

void globals_init() {
  flags = {0};
  challenge = {0};
  something_with_cpu_temp = true;

  /* Version counter initialization */
  version_byte = 1;

  /* Initialize control state variables */
  state.standby_power = power_standby_state::initial;
  state.fan_control = fan_control_state::initial;
  state.boot_challenge = boot_challenge_state::initial;
  state.leds = led_state::initial;
  state.audio_clamp = audio_state::clamped;
  state.pll_reset = pll_sysreset_state::initial;
  state.update_eject_tray = update_eject_tray_state::initial;
  state.dvd_tray_3 = update_dvd_tray3::initial;
  state.pwr_sw = power_switch_state::wait_for_button_press;
  state.eject_sw = eject_switch_state::wait_for_button_press;
  state.smi_power = smi_power_state::decision_state;
  state.dvd_tray = dvd_tray_state::initial;
  state.tray_eject = 0;
  state.video_mode = 0;

  /* Initialize flags */
  resetInterruptReason();
  resetStatus();
  flags.bitfield_DATA_6F = 0x02; /* Set power-off flag initially */
  flags.bitfield_DATA_70 = 0;
  flags.bitfield_DATA_71 = 0;
  flags.bitfield_DATA_72 = 0;
  flags.bitfield_DATA_73 = 0;
  flags.bitfield_DATA_74 = 0;

  /* Initialize sensor/config variables */
  sensors.fan_speed = 0;
  sensors.CPU_temperature = 0;
  sensors.board_temperature = 0;
  sensors.CPU_temp_predicted = 0;
  sensors.tray_status = 0;
  sensors.vmode = 0;
  sensors.vmode_raw = 0;

  config.custom_fan_speed = 10; /* Default custom fan speed */
  config.LED_green_manual_cycles = 0;
  config.LED_red_manual_cycles = 0;

  /* Initialize timer variables */
  timers.power_timeout = 0;
  timers.power_timeout2 = 0;
  timers.power_timeout3 = 0;
  timers.boot_response_timeout = 0;
  timers.audio_clamp_timeout = 0;
  timers.fan_control_timeout1 = 0;
  timers.fan_control_timeout2 = 0;

  leds.green_phases = 0;
  leds.red_phases = 0;
}

void set_fan_speed() {
  // Done, untested
  if (sensors.fan_speed > 50) {
    sensors.fan_speed = 50;
  }

  if (sensors.fan_speed == 0) {
    pico_hal::set_fan_off();
  } else {
    pico_hal::set_fan_speed(pwm_duty[sensors.fan_speed]);
    pico_hal::set_fan_on();
  }
}

void read_temperatures() {
  // Function only valid for 1.0-1.4

  // TODO: CPU
  sensors.CPU_temperature = 55;
  sensors.CPU_temperature_previous = sensors.CPU_temperature;
  sensors.board_temperature = 55;
  // if (i2c_read(I2C_TEMP_SENSOR_ADDRESS, 0x01, &(sensors.CPU_temperature))) {
  //   sensors.CPU_temperature_previous = sensors.CPU_temperature;
  // }

  // Board, near MCPX for 1.0-1.4
  // i2c_read(I2C_TEMP_SENSOR_ADDRESS, 0x02, &(sensors.board_temperature));
}

void boot_challenge_compute() {
  // Done, untested
  challenge.response0 = 0x33;
  challenge.response1 = 0xed;

  uint8_t challenge_loop_count = 4;
  do {
    challenge.response0 =
        (challenge.input_byte0 << 2 ^ challenge.input_byte1 + 0x39 ^ challenge.input_byte2 >> 2 ^ challenge.input_byte3 + 99U ^ challenge.response1) +
        challenge.response0;
    challenge.status_byte0 = challenge.input_byte0 + 0xbU;
    challenge.status_byte1 = challenge.input_byte1 >> 2;
    challenge.status_byte2 = challenge.input_byte2 + 0x1b;
    challenge.status_byte3 = 0;
    challenge.status_byte4 = challenge.response1;
    challenge.response1 =
        (challenge.input_byte0 + 0xbU ^ challenge.input_byte1 >> 2 ^ challenge.input_byte2 + 0x1b ^ challenge.response0) + challenge.response1;
    challenge_loop_count = challenge_loop_count + 0xff;
  } while (challenge_loop_count != 0);
}

} // namespace SMC
