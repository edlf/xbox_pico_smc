#include "smc.hpp"
#include "debug.hpp"

// Pico stuff
#include "hardware/i2c.h"
#include "hardware/watchdog.h"
#include "pico/i2c_slave.h"
#include "pico/stdlib.h"
#include "hal.hpp"
#include "pin_assignments.hpp"
#include "web.hpp"

namespace SMC {

volatile uint8_t ram_test_response0;
volatile uint8_t ram_test_response1;
volatile uint8_t bios_response_byte0;

flags_struct flags;
sensors_struct sensors;

void resetInterruptReason() {
  flags.interrupt_reason = 0;
}

void setInterruptReason(uint8_t ir) {
  flags.interrupt_reason |= ir;
}

void fireSystemInterrupt() {
  debug::print_message("SMC: Fire System Interrupt");

  /* Pulse SMI signal (RA4) low to signal system interrupt. The PIC holds it
   * low for 5 instruction cycles (2 us @ 10 MHz), see 0x68C */
  hal::smi_pin_off();
  busy_wait_us(2);
  hal::smi_pin_on();
}

// P01 (0x5FB): the PIC's only encoder write, register 0xBA = 0x3F
void configureConexantEncoder() {
  debug::print_message("SMC: Configure Conexant Encoder");

  constexpr size_t len = 2;
  uint8_t config[len] = {0xBA, 0x3F};
  if (!smbusMasterWrite(I2C_CONEXANT_ADDRESS, config, len)) {
    debug::print_warn("Conexant encoder write failed");
  }
}

// P11 (0x60C): ADM1032 configuration register = 0x00, running (not standby)
// with the ALERT output enabled
void configureTemperatureSensor() {
  constexpr size_t len = 2;
  uint8_t config[len] = {0x09, 0x00};
  if (!smbusMasterWrite(I2C_TEMP_SENSOR_ADDRESS, config, len)) {
    debug::print_warn("Temperature sensor write failed");
  }
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
  Led::setManualControl(flags.led_override);
  Led::setOverheat(flags.overheated);
  Led::setAvMissing(flags.av_cable_missing);
  // system_running is set while the system is powered
  Led::setPowerOff(!flags.system_running);
  Led::setQuickGreenBlink(flags.tray_moving);
  Led::setBootFailure(flags.frag_power_off); // Set once the boot challenge gives up
  Led::update();
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
#if SMC_WEB_INTERFACE
  Web::start();
#endif
  hal::timer1_init(hal::TICK_US);

  do {
    BootChallenge::seedStandby();
#if SMC_WEB_INTERFACE
    Web::service();
#endif

    // Power cycle: press the power button for the host
    if (SMI::takePowerOnRequest()) {
      flags.power_change_requested = true;
    }

    uint8_t power_standby_state = PowerStandby::update();
    if ((power_standby_state & 1) != 0) {
      BootChallenge::seedPowerUp();

      while (true) {
#if SMC_WEB_INTERFACE
        Web::service();
#endif
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
        if ((power_standby_state & 1) != 0) {
          break;
        }

        hal::timer1_wait();
        hal::petWatchdog();
      }

      Fan::powerOff();

      PowerStandby::init();
      Fan::init();
      Dvd::init();
      Video::init();
      AudioClamp::init();
      BootChallenge::init();
      Led::resetPhaseCounter();
      SMI::init();
      hal::petWatchdog();

      globals_init();

#if SMC_FIRMWARE_P11
      // P11 skips the wait when powering back on after a power cycle (0x2A2)
      // and waits one tick less otherwise (P2L, 0x2D5: the usual 25)
      const bool wait_after_power_off = !SMI::powerOnRequested();
      uint8_t wait_ticks = SMC_FIRMWARE_P2L ? 25 : 24;
#else
      const bool wait_after_power_off = true;
      uint8_t wait_ticks = 25;
#endif

      while (wait_after_power_off) {
        hal::timer1_wait();
        hal::petWatchdog();
        FrontPanelSW::updatePower();
        if (--wait_ticks == 0) {
          break;
        }
      }
    }

    printSMBusLog();
    hal::petWatchdog();
  } while (true);
}

void globals_init() {
  flags = {};

  resetInterruptReason();

  resetSMBusRegisters();

  // Same as the PIC's RAM_init (0xB58): all flags clear (above) except
  flags.tray_events_enabled = true; // First tray state change raises an interrupt

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

} // namespace SMC
