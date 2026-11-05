#include "smc.hpp"
#include "debug.hpp"

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

// P01: the PIC's only encoder write, register 0xBA = 0x3F
void configureConexantEncoder() {
  debug::print(debug::Level::Info, "SMC: Configure Conexant Encoder");

  constexpr size_t  len = 2;
  constexpr uint8_t config[len] = {0xBA, 0x3F};

  if (!smbusMasterWrite(I2C_CONEXANT_ADDRESS, config, len)) {
    debug::print(debug::Level::Warnings, "Conexant encoder write failed");
  }
}

// P11: ADM1032 configuration register = 0x00, running (not standby)
// with the ALERT output enabled
void configureTemperatureSensor() {
  if (Fan::temperatureBypass()) {
    return;  // Not used: nothing to configure, and it may not answer
  }
  constexpr size_t  len = 2;
  constexpr uint8_t config[len] = {0x09, 0x00};

  if (!smbusMasterWrite(I2C_TEMP_SENSOR_ADDRESS, config, len)) {
    debug::print(debug::Level::Warnings, "Temperature sensor write failed");
  }
}

// Mirrors the PIC's only external interrupt: CCP2 capture on a POWOK falling
// edge, armed while the Xbox is powered (see setPowerOkIrq). Buttons, tray and
// AV pins are polled by their state machines, like on the PIC.
void smc_gpio_callback(unsigned int gpio, [[maybe_unused]] uint32_t events) {
  if (gpio != pins::POWER_OK) {
    return;
  }

  // POWOK dropped while the Xbox was meant to be on: power supply failure.
  // panic() switches everything off and waits for the watchdog.
  if (!hal::get_power_ok()) {
    panic("POWER_OK lost");
  }
}

void setPowerOkIrq(bool enabled) {
  gpio_acknowledge_irq(pins::POWER_OK, GPIO_IRQ_EDGE_FALL);
  gpio_set_irq_enabled_with_callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL, enabled, &smc_gpio_callback);
}

void update_LEDs() {
  Led::setManualControl(Led::flags.led_override);
  Led::setOverheat(Fan::flags.overheated);
  Led::setAvMissing(Video::flags.av_cable_missing);
  Led::setPowerOff(!SMI::flags.system_running);
  Led::setQuickGreenBlink(Dvd::flags.tray_moving);
  // Once the boot challenge gives up (FRAG), or a failed RAM test
  Led::setBootFailure(BootChallenge::flags.frag_power_off || BootChallenge::ramTestFailed());
  Led::update();
  RgbLed::update();
}

void main_loop() {
  globals_init();
  hal::init();
  hal::pico_led_on();

  shutdown_xbox();

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
  ExtraTemp::init();
  RgbLed::init();
  Settings::load();    // After the modules it sets
  I2CSniffer::init();  // After the settings: started only when it's on
  Settings::printSettings();

  debug::print(debug::Level::Info, "MAIN: Set I2C interrupt");
  i2c_slave_init(I2C_PORT, I2C_SLAVE_ADDRESS, &handle_SMBus_interrupt);

  hal::enableWatchdog();
#if SMC_WEB_INTERFACE
  Web::start();
#endif
  hal::timer1_init(hal::TICK_US);

  do {
    BootChallenge::seedStandby();
    applySMBusWrites();
#if SMC_WEB_INTERFACE
    Web::service();
#endif
    SerialConsole::service();

    // Power cycle: press the power button for the host
    if (SMI::takePowerOnRequest()) {
      SMI::flags.power_change_requested = true;
    }

    uint8_t power_standby_state = PowerStandby::update();
    if ((power_standby_state & 1) != 0) {
      BootChallenge::seedPowerUp();

      while (true) {
#if SMC_WEB_INTERFACE
        Web::service();
#endif
        SerialConsole::service();
        applySMBusWrites();
        PLL_Reset::update();
        BootChallenge::update();
        Dvd::updateTrayDebounce();
        Dvd::updateEjectPulse();
        Video::update();
        FrontPanelSW::update();
        Dvd::updateEjectRequests();
        Dvd::updateTrayMoving();
        AudioClamp::update();
        update_LEDs();
        Fan::update_fan_temp();
        Fan::updateGlide();
        ExtraTemp::update();
        I2CSniffer::update();

        // Again, so an OS answer (SMBus 0x0D) that came in during this tick is
        // seen now, as when the interrupt set the flags itself: the OS's
        // answer windows (25 ticks for a power off) keep their full length
        applySMBusWrites();
        power_standby_state = SMI::update();
        printSMBusLog();
        if ((power_standby_state & 1) != 0) {
          break;
        }

        hal::timer1_wait();
        hal::petWatchdog();
      }

      resetAfterPowerOff();

      // As P2L: no wait when powering back on after a power cycle,
      // 25 ticks otherwise (P11 waits 24, P01 always waits 25)
      const bool wait_after_power_off = !SMI::powerOnRequested();
      uint8_t    wait_ticks = 25;

      while (wait_after_power_off) {
        hal::timer1_wait();
        hal::petWatchdog();
        FrontPanelSW::updatePower();
        update_LEDs();  // Keeps the power down fade going
        if (--wait_ticks == 0) {
          break;
        }
      }
    }

    printSMBusLog();
    hal::petWatchdog();
  } while (true);
}

void resetAfterPowerOff() {
  Fan::powerOff();

  PowerStandby::init();
  Fan::init();
  Dvd::init();
  Video::init();
  AudioClamp::init();
  BootChallenge::init();
  Led::resetPhaseCounter();
  SMI::init();
  // As the PIC: the next power on starts it from the top again, which turns
  // reset-on-eject off and lets the first SMBus 0x19 write decide. Without it
  // globals_init() leaves reset-on-eject on and locked, and opening the tray
  // resets the Xbox.
  PLL_Reset::init();
  hal::petWatchdog();

  globals_init();
}

void globals_init() {
  SMI::flags = {};
  Dvd::flags = {};
  Video::flags = {};
  AudioClamp::flags = {};
  Fan::flags = {};
  Led::flags = {};
  PLL_Reset::flags = {};
  BootChallenge::flags = {};

  SMI::resetInterruptReason();
  I2CSniffer::resetFound();

  resetSMBusRegisters();

  // First tray state change raises an interrupt
  Dvd::flags.tray_events_enabled = true;

  Fan::sensors.CPU_temperature = 0x17;  // 23 °C, as the PIC
  Fan::sensors.board_temperature = 0x17;
  Dvd::sensors.tray_status = Dvd::drive_tray_state::closed;
  Video::sensors.vmode = Video::av_pack::scart;
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

void panic(const char* message) {
  hal::disable_and_discard_interrupts();

  shutdown_xbox();

  debug::print(debug::Level::Critical, "%s Waiting for watchdog to reboot", message);

  while (true) {
    hal::pico_led_on();
    busy_wait_ms(500);
    hal::pico_led_off();
    busy_wait_ms(500);
  }
}

}  // namespace SMC
