#include "smc.hpp"
#include "smc_types.hpp"
#include "debug.hpp"
#include "utils.hpp"
#include "smc_audio_clamp.hpp"
#include "smc_fan.hpp"
#include "smc_boot_challenge.hpp"
#include "smc_smbus.hpp"
#include "smc_dvd.hpp"

// Pico stuff
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "i2c_slave/include/i2c_slave.h"
#include "pico/stdlib.h"
#include "pico_hal.hpp"
#include "pin_assignments.hpp"

namespace SMC {

bool something_with_cpu_temp;
volatile uint8_t ram_test_response0;
volatile uint8_t ram_test_response1;
volatile uint8_t bios_response_byte0;
volatile uint8_t bios_response_byte1;

state_struct state;
state_struct state_previous;
flags_struct flags;
sensors_struct sensors;
timers_struct timers;
config_struct config;

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

void setStatusBit(uint8_t status_bit) {
  flags.status |= status_bit;
}

void clearStatusBit(uint8_t status_bit) {
  flags.status &= status_bit;
}

uint8_t checkStatusBit(uint8_t status_bit) {
  return flags.status & status_bit;
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

void smc_gpio_callback(unsigned int gpio, uint32_t events) {
  uint32_t int_status = pico_hal::disableInterrupts();
  // bit 3 - AV mode changed
  // InterruptReason_av_mode_changed = 0b00001000,   // 0x08
  // bit 4 - AV cable unplugged
  // InterruptReason_av_unplugged = 0b00010000,      // 0x10

  switch (gpio) {
  case pins::SW_EJECT:
    debug::print_message("ISRL: Eject switch");
    setInterruptReason(InterruptReason_eject_sw_pressed);
    break;

  case pins::POWER_OK:
    // ISR got called, but somehow power good didnt stay enabled
    if (!pico_hal::get_power_ok()) {
      // Power supply failure - shutdown sequence
      pico_hal::shutdown_xbox();

      // Clear the POWOK interrupt flag and wait for watchdog reset
      resetInterruptReason();
      resetStatus();

      // Enter infinite loop waiting for watchdog timer to reset the system
      pico_hal::panic("POWER_OK timeout");
    }

    debug::print_message("ISR: power OK");
    break;

  case pins::SW_POWER:
    debug::print_message("ISR: Power switch");
    setInterruptReason(InterruptReason_power_sw_pressed);
    break;

  case pins::VIDEO_MODE_0:
    debug::print_message("ISR: vm0");
    Video::update();
    break;

  case pins::VIDEO_MODE_1:
    debug::print_message("ISR: vm1");
    Video::update();
    break;

  case pins::VIDEO_MODE_2:
    debug::print_message("ISR: vm2");
    Video::update();
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

void init_irqs() {
  debug::print_message("MAIN: Set gpio IRQs");
  gpio_set_irq_enabled_with_callback(pins::SW_POWER, GPIO_IRQ_EDGE_FALL, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::SW_EJECT, GPIO_IRQ_EDGE_FALL, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::DVD_EJECT, GPIO_IRQ_EDGE_FALL, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::VIDEO_MODE_0, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::VIDEO_MODE_1, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::VIDEO_MODE_2, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::TRAY_STATE_0, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::TRAY_STATE_1, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
  gpio_set_irq_enabled_with_callback(pins::TRAY_STATE_2, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &smc_gpio_callback);
}

void update_LEDs() {
  Led::setManualControl(utils::checkBitNo(flags.bitfield_DATA_70, 2));
  Led::setOverheat(checkStatusBit(overheated));
  Led::setAvMissing(utils::checkBitNo(flags.bitfield_DATA_71, 6));
  Led::setPowerOff(utils::checkBitNo(flags.bitfield_DATA_70, 3));
  Led::setQuickGreenBlink(utils::checkBitNo(flags.bitfield_DATA_6F, 5));
  Led::update();
  // Led::printState();
}

void main_loop() {
  globals_init();
  pico_hal::init();

  // Fail safes
  pico_hal::assertSystemReset();
  pico_hal::turn_off_psu(); // Shut off system if we rebooted

  init_irqs();
  pico_hal::set_fan_off();
  pico_hal::audio_clamp_on();

  gpio_put(pins::RP2040_LED, 1);

  debug::print_message("MAIN: Set I2C interrupt");
  i2c_slave_init(i2c0, I2C_SLAVE_ADDRESS, &handle_SMBus_interrupt);

  /* Initialize state variables */
  state.standby_power = power_standby_state::initial;
  state.pwr_sw = power_switch_state::wait_for_button_press;
  state.eject_sw = eject_switch_state::wait_for_button_press;

  state.fan_control = fan_control_state::initial;
  state.dvd_tray = dvd_tray_state::initial;
  Video::init();
  state.audio_clamp = audio_state::clamped;
  Led::resetPhaseCounter();
  state.smi_power = smi_power_state::initial;
  state.tray_eject = 1;

  timers.power_timeout3 = 1;

  uint32_t loop_count = 0;

  pico_hal::enableWatchdog();

  BootChallenge::challenge_struct& challenge = BootChallenge::getChallengeStructRef();

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
        BootChallenge::update();
        update_dvd_tray();
        update_eject_tray();
        Video::update();
        update_pwr_sw();
        update_eject_sw();
        update_dvd_tray_eject();
        update_dvd_tray_three();
        AudioClamp::update();
        update_LEDs();
        update_fan_temp();

        power_standby_state = update_SMI_and_power();
        printStateSMI();
        if ((power_standby_state & 1) != 0) {
          break;
        }

        busy_wait_ms(40);
        wait_for_isr();
        pico_hal::petWatchdog();
      }

      sensors.fan_speed = 0;
      set_fan_speed();

      state.standby_power = power_standby_state::initial;
      // jump_index_sub_code_828 = 0;
      state.fan_control = fan_control_state::initial;
      state.dvd_tray = dvd_tray_state::initial;
      Video::init();
      state.audio_clamp = audio_state::clamped;
      // jump_index_sub_code_5AF = 0;
      BootChallenge::resetState();
      state.dvd_tray_three = update_dvd_tray_three_state::initial; // jump_index_sub_code_519
      Led::resetPhaseCounter();
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

    busy_wait_ms(40);

    pico_hal::petWatchdog();
    loop_count++;
  } while (true);
}

void globals_init() {
  AudioClamp::init();
  BootChallenge::init();
  Led::init();
  Video::init();

  flags = {0};
  something_with_cpu_temp = true;

  state.standby_power = power_standby_state::initial;
  state.fan_control = fan_control_state::initial;
  state.pll_reset = pll_sysreset_state::initial;
  state.update_eject_tray = update_eject_tray_state::initial;
  state.dvd_tray_three = update_dvd_tray_three_state::initial;
  state.pwr_sw = power_switch_state::wait_for_button_press;
  state.eject_sw = eject_switch_state::wait_for_button_press;
  state.smi_power = smi_power_state::decision_state;
  state.dvd_tray = dvd_tray_state::initial;
  state.tray_eject = 0;

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
  timers.fan_control_timeout1 = 0;
  timers.fan_control_timeout2 = 0;
}

} // namespace SMC
