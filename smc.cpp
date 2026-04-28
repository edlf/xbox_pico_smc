#include "smc.hpp"
#include "debug.hpp"
#include "utils.hpp"

// Pico stuff
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "i2c_slave/include/i2c_slave.h"
#include "pico/stdlib.h"
#include "hal.hpp"
#include "pin_assignments.hpp"

namespace SMC {

bool something_with_cpu_temp;
volatile uint8_t ram_test_response0;
volatile uint8_t ram_test_response1;
volatile uint8_t bios_response_byte0;
volatile uint8_t bios_response_byte1;

flags_struct flags;
sensors_struct sensors;
timers_struct timers;

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
  debug::print_message("SMC: Fire System Interrupt");

  /* Pulse SMI signal (RA4) low to signal system interrupt */
  hal::smi_pin_off();
  sleep_ms(20);
  hal::smi_pin_on();
}

void configureEncoder() {
  Encoder encoder = DetectVideoEncoder();

  switch (encoder) {
    case Encoder::Conexant:
      configureConexantEncoder();
      break;

    case Encoder::Focus:
      configureFocusEncoder();
      break;

    case Encoder::Xcalibur:
      configureXcaliburEncoder();
      break;

    default:
      debug::print_critical("No encoder returned");
      break;
  }
}

void configureConexantEncoder() {
  debug::print_message("SMC: Configure Conexant Encoder");

  constexpr size_t len = 2;
  uint8_t config[len] = {0xBA, 0x3F};
  i2c_write_burst_blocking(I2C_PORT, I2C_CONEXANT_ADDRESS, config, len);
}

void configureFocusEncoder() {
  // TODO
}

void configureXcaliburEncoder() {
  // TODO
}

void smc_gpio_callback(unsigned int gpio, uint32_t events) {
  uint32_t int_status = hal::disableInterrupts();
  // bit 3 - AV mode changed
  // InterruptReason_av_mode_changed = 0b00001000,   // 0x08
  // bit 4 - AV cable unplugged
  // InterruptReason_av_unplugged = 0b00010000,      // 0x10

  switch (gpio) {
  case pins::SW_EJECT:
    debug::print_message("ISR: Eject switch");
    setInterruptReason(InterruptReason_eject_sw_pressed);
    break;

  case pins::POWER_OK:
    debug::print_message("ISR: power OK");

    // ISR got called, but somehow power good didnt stay enabled
    if (!hal::get_power_ok()) {
      // Power supply failure - shutdown sequence
      shutdown_xbox();

      // Clear the POWOK interrupt flag and wait for watchdog reset
      resetInterruptReason();
      resetStatus();

      // Enter infinite loop waiting for watchdog timer to reset the system
      panic("POWER_OK timeout");
    }
    break;

  case pins::SW_POWER:
    debug::print_message("ISR: Power switch");
    setInterruptReason(InterruptReason_power_sw_pressed);
    break;

  case pins::VIDEO_MODE_0:
    debug::print_message("ISR: AV Port Pin 0");
    Video::update();
    break;

  case pins::VIDEO_MODE_1:
    debug::print_message("ISR: AV Port Pin 1");
    Video::update();
    break;

  case pins::VIDEO_MODE_2:
    debug::print_message("ISR: AV Port Pin 2");
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
  hal::reenableInterrupts(int_status);
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

// TODO: Shouldnt be needed in the end...
void checkEmergencyOff() {
  constexpr uint32_t hold_time = 5000;
  static bool was_pressed = false;
  static uint32_t last_press_time = 0;

  const uint32_t current_time = hal::get_ms_since_boot();
  const bool pwr_pressed = hal::power_button_pressed();

  if (pwr_pressed && !was_pressed) {
    was_pressed = true;
    last_press_time = hal::get_ms_since_boot();
    return;
  }

  if (was_pressed && pwr_pressed) {
    if (current_time - last_press_time >= hold_time) {
      shutdown_xbox();
    }

    return;
  }

  if (!pwr_pressed) {
    was_pressed = false;
  }
}

uint32_t loop_start_time = 0;

void main_loop() {
  globals_init();
  hal::init();
  hal::pico_led_on();

  // Make sure the xbox is turned off
  shutdown_xbox();

  // Initialize state machines
  hal::set_fan_off();
  hal::audio_clamp_on();
  PowerStandby::init();
  FrontPanelSW::init();
  Fan::init();
  Dvd::init();
  Video::init();
  AudioClamp::init();
  Led::init();
  SMI::init();
  BootChallenge::init();
  PLL_Reset::init();

  init_irqs();

  debug::print_message("MAIN: Set I2C interrupt");
  i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDRESS, &handle_SMBus_interrupt);

  hal::enableWatchdog();

  timers.power_timeout3 = 1;
  BootChallenge::challenge_struct& challenge = BootChallenge::getChallengeStructRef();

  do {
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

    uint8_t power_standby_state = PowerStandby::update();
    if ((power_standby_state & 1) != 0) {
      // Get timer + cpu temp for entropy
      challenge.status_byte2 = challenge.status_byte0 + 22; // TODO Feed number from timer
      challenge.status_byte3 = challenge.status_byte1 ^ sensors.CPU_temperature;

      while (true) {
        loop_start_time = hal::get_ms_since_boot();

        // TODO: This should be removed
        checkEmergencyOff();

        PLL_Reset::update();
        BootChallenge::update();
        Dvd::updateDvdTray();
        Dvd::updateEjectTray();
        Video::update();
        FrontPanelSW::update();
        Dvd::updateDvdTrayEject();
        Dvd::updateDvdTrayThree();
        AudioClamp::update();
        update_LEDs();
        Fan::update_fan_temp();

        power_standby_state = SMI::update();
        // SMI::printState();
        if ((power_standby_state & 1) != 0) {
          break;
        }

        busy_wait_ms(40);
        wait_for_isr();
        hal::petWatchdog();
      }

      Fan::applyFanSpeed();

      PowerStandby::init();
      // jump_index_sub_code_828 = 0;
      Fan::init();
      Dvd::init();
      Video::init();
      AudioClamp::init();
      // jump_index_sub_code_5AF = 0;
      BootChallenge::resetState();
      Led::resetPhaseCounter();
      SMI::init();
      hal::petWatchdog();

      globals_init();
      timers.power_timeout3 = 25;

      do {
        wait_for_isr();
        hal::petWatchdog();
        FrontPanelSW::updatePower();
        timers.power_timeout3--;
      } while (timers.power_timeout3 != 0);

      timers.power_timeout3 = 0;
    } else {
      loop_start_time = hal::get_ms_since_boot();
    }

    busy_wait_ms(40);

    hal::petWatchdog();
  } while (true);
}

void globals_init() {
  flags = {0};
  something_with_cpu_temp = true;

  resetInterruptReason();
  resetStatus();

  flags.bitfield_DATA_6F = 0x02; /* Set power-off flag initially */
  flags.bitfield_DATA_70 = 0;
  flags.bitfield_DATA_71 = 0;
  flags.bitfield_DATA_72 = 0;
  flags.bitfield_DATA_73 = 0;
  flags.bitfield_DATA_74 = 0;

  sensors.CPU_temperature = 0;
  sensors.board_temperature = 0;
  sensors.CPU_temp_predicted = 0;
  sensors.tray_status = 0;
  sensors.vmode = 0;
  sensors.vmode_raw = 0;

  timers.power_timeout = 0;
  timers.power_timeout3 = 0;
}

void shutdown_xbox() {
  hal::assertSystemReset();
  hal::PLL_off();
  hal::led_green_off();
  hal::led_red_off();
  hal::set_fan_off();
  hal::audio_clamp_off();
  hal::turn_off_psu();
}

void panic(const std::string message) {
  hal::disable_and_discard_interrupts();
  hal::timer0_disable();
  hal::timer1_disable();

  shutdown_xbox();

  debug::print_critical(message + " Waiting for watchdog to reboot");

  while (true) {
    hal::pico_led_on();
    sleep_ms(500);
    hal::pico_led_off();
    sleep_ms(500);
  }
}

Encoder DetectVideoEncoder(void)
{
  /* TODO: Based on XBLAST
  if (I2CTransmitByteGetReturn(I2C_CONEXANT_ADDRESS, 0x00) != ERR_I2C_ERROR_BUS)
  {
    return Encoder::Conexant;
  }
  else if (I2CTransmitByteGetReturn(I2C_FOCUS_ADDRESS, 0x00) != ERR_I2C_ERROR_BUS)
  {
    return Encoder::Focus;
  }
  else
  {
    return Encoder::Xcalibur;
  }
  */
  return Encoder::Conexant;
}

} // namespace SMC
