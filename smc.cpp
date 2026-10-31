#include "smc.hpp"
#include "debug.hpp"
#include "utils.hpp"

// Pico stuff
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/i2c_slave.h"
#include "pico/stdlib.h"
#include "hal.hpp"
#include "pin_assignments.hpp"

namespace SMC {

volatile uint8_t ram_test_response0;
volatile uint8_t ram_test_response1;
volatile uint8_t bios_response_byte0;
volatile uint8_t bios_response_byte1;

flags_struct flags;
sensors_struct sensors;

void resetInterruptReason() {
  flags.interrupt_reason = 0;
}

void setInterruptReason(uint8_t ir) {
  flags.interrupt_reason |= ir;
}

void clearInterruptReason(uint8_t ir) {
  flags.interrupt_reason &= ~ir;
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
  flags.status &= ~status_bit;
}

uint8_t checkStatusBit(uint8_t status_bit) {
  return flags.status & status_bit;
}

void fireSystemInterrupt() {
  debug::print_message("SMC: Fire System Interrupt");

  /* Pulse SMI signal (RA4) low to signal system interrupt. The PIC holds it
   * low for 5 instruction cycles (2 us @ 10 MHz), see 0x68C */
  hal::smi_pin_off();
  busy_wait_us(2);
  hal::smi_pin_on();
}

void configureEncoder() {
  debug::print_message("SMC: Configure Encoder");
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
  if (!smbusMasterWrite(I2C_CONEXANT_ADDRESS, config, len)) {
    debug::print_warn("Conexant encoder write failed");
  }
}

void configureFocusEncoder() {
  // TODO
}

void configureXcaliburEncoder() {
  // TODO
}

// Mirrors the PIC's only external interrupt: CCP2 capture on a POWOK falling
// edge, armed while the Xbox is powered (see setPowerOkIrq). Buttons, tray and
// AV pins are polled by their state machines, like on the PIC.
void smc_gpio_callback(unsigned int gpio, uint32_t events) {
  if (gpio != pins::POWER_OK) {
    return;
  }

  debug::print_message("ISR: power OK");

  // ISR got called, but somehow power good didnt stay enabled
  if (!hal::get_power_ok()) {
    // Power supply failure - shutdown sequence
    shutdown_xbox();

    // Enter infinite loop waiting for watchdog timer to reset the system
    panic("POWER_OK lost");
  }
}

void setPowerOkIrq(bool enabled) {
  gpio_acknowledge_irq(pins::POWER_OK, GPIO_IRQ_EDGE_FALL);
  gpio_set_irq_enabled_with_callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL, enabled, &smc_gpio_callback);
}

void update_LEDs() {
  Led::setManualControl(utils::checkBitNo(flags.bitfield_DATA_70, 2));
  Led::setOverheat(checkStatusBit(overheated));
  Led::setAvMissing(utils::checkBitNo(flags.bitfield_DATA_71, 6));
  // 70.3 is set while the system is powered
  Led::setPowerOff(!utils::checkBitNo(flags.bitfield_DATA_70, 3));
  Led::setQuickGreenBlink(utils::checkBitNo(flags.bitfield_DATA_6F, 5));
  Led::update();
  // Led::printState();
}

void main_loop() {
  globals_init();
  hal::init();
  hal::pico_led_on();

  // Make sure the xbox is turned off
  shutdown_xbox();

  // Initialize state machines
  hal::fan_pin_low();
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

  debug::print_message("MAIN: Set I2C interrupt");
  i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDRESS, &handle_SMBus_interrupt);

  hal::enableWatchdog();
  hal::timer1_init(hal::TICK_US);

  PowerStandby::setPowerTimeout3(1);
  do {
    BootChallenge::seedStandby();

    // Power cycle: press the power button for the host
    if (SMI::takePowerOnRequest()) {
      setStatusBit(power_change_requested);
    }

    uint8_t power_standby_state = PowerStandby::update();
    if ((power_standby_state & 1) != 0) {
      BootChallenge::seedPowerUp();

      while (true) {
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
        printSMBusLog();
        // SMI::printState();
        if ((power_standby_state & 1) != 0) {
          break;
        }

        hal::timer1_wait();
        hal::petWatchdog();
      }

      Fan::powerOff();

      PowerStandby::init();
      // jump_index_sub_code_828 = 0;
      Fan::init();
      Dvd::init();
      Video::init();
      AudioClamp::init();
      // jump_index_sub_code_5AF = 0;
      BootChallenge::init();
      Led::resetPhaseCounter();
      SMI::init();
      hal::petWatchdog();

      globals_init();
      PowerStandby::setPowerTimeout3(25);

      do {
        hal::timer1_wait();
        hal::petWatchdog();
        FrontPanelSW::updatePower();
        PowerStandby::powerTimeout3Decrement();
      } while (PowerStandby::getPowerTimeout3() != 0);

      PowerStandby::setPowerTimeout3(0);
    }

    printSMBusLog();
    hal::petWatchdog();
  } while (true);
}

void globals_init() {
  flags = {0};

  resetInterruptReason();
  resetStatus();

  resetSMBusRegisters();

  // Same as the PIC's RAM_init (0xB58)
  flags.bitfield_DATA_6F = 0;
  flags.bitfield_DATA_70 = 0;
  flags.bitfield_DATA_71 = 0;
  flags.bitfield_DATA_72 = 0x10; // First tray state change raises an interrupt
  flags.bitfield_DATA_73 = 0;
  flags.bitfield_DATA_74 = 0;

  sensors.CPU_temperature = 0x17;
  sensors.board_temperature = 0x17;
  sensors.tray_status = 0;
  sensors.vmode = 0;
}

void shutdown_xbox() {
  hal::assertSystemReset();
  hal::PLL_off();
  hal::led_green_off();
  hal::led_red_off();
  hal::fan_pin_low();
  hal::audio_clamp_off();
  hal::turn_off_psu();
}

void panic(const std::string message) {
  hal::disable_and_discard_interrupts();

  shutdown_xbox();

  debug::print_critical(message + " Waiting for watchdog to reboot");

  while (true) {
    hal::pico_led_on();
    busy_wait_ms(500); // Interrupts are off, sleep_ms needs them
    hal::pico_led_off();
    busy_wait_ms(500);
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
