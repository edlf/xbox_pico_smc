#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <pico/sync.h>

#include "hal.hpp"
#include "pin_assignments.hpp"
#include "debug.hpp"

namespace hal {

// FAN PWM
pwm_config pwm_cfg;
uint pwm_slice;
uint pwm_channel;

// Front panel LEDs are active low, like on the PIC (RA0/RA1 low = on)
void led_green_on() {
  gpio_put(pins::LED_GREEN, 0);
}

void led_green_off() {
  gpio_put(pins::LED_GREEN, 1);
}

void led_red_on() {
  gpio_put(pins::LED_RED, 0);
}

void led_red_off() {
  gpio_put(pins::LED_RED, 1);
}

void pico_led_on() {
  gpio_put(pins::RP2040_LED, 1);
}

void pico_led_off() {
  gpio_put(pins::RP2040_LED, 0);
}

bool state_pll = false;

void PLL_on() {
  gpio_put(pins::PLL_ENABLE, 1);

  if (!state_pll) {
    debug::print_message("GPIO: Assert PLL");
    state_pll = true;
  }
}

void PLL_off() {
  gpio_put(pins::PLL_ENABLE, 0);

  if (state_pll) {
    debug::print_message("GPIO: Deassert PLL");
    state_pll = false;
  }
}

void smi_pin_on() {
  gpio_put(pins::SMI, 1);
}

void smi_pin_off() {
  gpio_put(pins::SMI, 0);
}

void audio_clamp_on() {
  gpio_put(pins::AUDIO_CLAMP, 1);
}

void audio_clamp_off() {
  gpio_put(pins::AUDIO_CLAMP, 0);
}

bool state_psu = false;

void turn_on_psu() {
  gpio_put(pins::POWER_ON, 1);

  if (!state_psu) {
    debug::print_message("GPIO: Turn on xbox PSU");
    state_psu = true;
  }
}

void turn_off_psu() {
  gpio_put(pins::POWER_ON, 0);

  if (state_psu) {
    debug::print_message("GPIO: Turn off xbox PSU");
    state_psu = false;
  }
}

// Open drain like the PIC (TRISA.5): pull the line low to assert, otherwise let
// the line's 1k pull-up hold it high
void dvd_eject_assert() {
  gpio_set_dir(pins::DVD_EJECT, GPIO_OUT);
}

void dvd_eject_release() {
  gpio_set_dir(pins::DVD_EJECT, GPIO_IN);
}

void rtc_dump_on() {
  gpio_put(pins::RTC_DUMP, 1);
}

void rtc_dump_off() {
  gpio_put(pins::RTC_DUMP, 0);
}

bool get_power_ok() {
  return gpio_get(pins::POWER_OK);
}

// Gets video cable type
uint8_t get_video_mode() {
  uint8_t video_mode = 0;

  // Video mode is set as 0b0000_1110
  if (gpio_get(pins::VIDEO_MODE_0)) {
    video_mode = video_mode | (1 << 1);
  }
  if (gpio_get(pins::VIDEO_MODE_1)) {
    video_mode = video_mode | (1 << 2);
  }
  if (gpio_get(pins::VIDEO_MODE_2)) {
    video_mode = video_mode | (1 << 3);
  }

  return video_mode;
}

uint8_t get_tray_state() {
  uint8_t tray_state = 0;
  // Tray state is set as 0b0111_0000
  if (gpio_get(pins::TRAY_STATE_0)) {
    tray_state = tray_state | (1 << 4);
  }
  if (gpio_get(pins::TRAY_STATE_1)) {
    tray_state = tray_state | (1 << 5);
  }
  if (gpio_get(pins::TRAY_STATE_2)) {
    tray_state = tray_state | (1 << 6);
  }

  // debug::print_message("GPIO: Get tray state " + std::to_string(tray_state));
  return tray_state;
}

bool power_button_pressed() {
  // High is not pressed
  return !gpio_get(pins::SW_POWER);
}

bool eject_button_pressed() {
  // High is not pressed
  return !gpio_get(pins::SW_EJECT);
}

// Fan PWM, same output as the PIC's CCP1 (0xE85): ~9.8 kHz, 10-bit, and
// inverted. Speed 0 holds the pin high, 50 holds it low, and 1-49 keep it
// high for (50 - speed)% of each period.
constexpr uint16_t FAN_PWM_WRAP = 1023;
constexpr uint32_t FAN_PWM_HZ = 9766; // PIC: 10 MHz / 4 / 256 (PR2 = 0xFF)

static uint8_t fan_speed_set = 0xFF;

void set_fan_speed(uint8_t speed) {
  if (speed > FAN_MAX_SPEED) {
    speed = FAN_MAX_SPEED;
  }

  uint16_t level;
  if (speed == 0) {
    level = FAN_PWM_WRAP + 1; // Always high
  } else {
    level = (uint32_t)(FAN_PWM_WRAP + 1) * (FAN_MAX_SPEED - speed) / (2 * FAN_MAX_SPEED);
  }
  pwm_set_chan_level(pwm_slice, pwm_channel, level);

  if (speed != fan_speed_set) {
    debug::print_message("GPIO: Fan speed " + std::to_string(speed));
    fan_speed_set = speed;
  }
}

void fan_pin_low() {
  pwm_set_chan_level(pwm_slice, pwm_channel, 0);
  fan_speed_set = 0xFF;
}

// SMBus master on the slave's own pins, bit-banged like the PIC does
// (0xC33, 0xC91). Switching the I2C block between slave and master doesn't
// work reliably once the host has used the slave, so the block stays a slave
// and only loses its pins for the duration of a transfer.
namespace {

constexpr uint32_t i2c_half_period_us = 5;          // ~100 kHz
constexpr uint32_t i2c_clock_stretch_timeout_us = 1000;
constexpr uint32_t i2c_bus_idle_us = 50;

// Open drain by hand: output low to pull down, input to let the pull-up win
void sclLow() { gpio_set_dir(pins::I2C_SCL, GPIO_OUT); }
void sdaLow() { gpio_set_dir(pins::I2C_SDA, GPIO_OUT); }
void sdaRelease() { gpio_set_dir(pins::I2C_SDA, GPIO_IN); }
void halfPeriod() { busy_wait_us(i2c_half_period_us); }

bool sclReleaseAndWait() {
  gpio_set_dir(pins::I2C_SCL, GPIO_IN);
  const absolute_time_t deadline = make_timeout_time_us(i2c_clock_stretch_timeout_us);
  while (!gpio_get(pins::I2C_SCL)) {
    if (time_reached(deadline)) {
      return false;
    }
  }
  return true;
}

void takePins() {
  gpio_put(pins::I2C_SCL, 0);
  gpio_put(pins::I2C_SDA, 0);
  gpio_set_dir(pins::I2C_SCL, GPIO_IN);
  gpio_set_dir(pins::I2C_SDA, GPIO_IN);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_SIO);
  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_SIO);
}

void returnPins() {
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
}

// Both lines must stay high for a while, so we don't cut into someone else's
// transfer (the input works whatever the pin function is)
bool busIdle() {
  const absolute_time_t deadline = make_timeout_time_us(i2c_bus_idle_us);
  while (!time_reached(deadline)) {
    if (!gpio_get(pins::I2C_SCL) || !gpio_get(pins::I2C_SDA)) {
      return false;
    }
  }
  return true;
}

I2CMasterResult start(bool repeated) {
  sdaRelease();
  halfPeriod();
  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }
  if (!gpio_get(pins::I2C_SDA)) {
    return repeated ? I2CMasterResult::arbitration_lost : I2CMasterResult::bus_busy;
  }
  halfPeriod();
  sdaLow();
  halfPeriod();
  sclLow();
  return I2CMasterResult::ok;
}

void stop() {
  sdaLow();
  halfPeriod();
  sclReleaseAndWait();
  halfPeriod();
  sdaRelease();
  halfPeriod();
}

I2CMasterResult writeBit(bool bit) {
  if (bit) {
    sdaRelease();
  } else {
    sdaLow();
  }
  halfPeriod();
  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }
  // Someone else pulling SDA low while we send a 1: they win the bus
  const bool lost = bit && !gpio_get(pins::I2C_SDA);
  halfPeriod();
  sclLow();
  return lost ? I2CMasterResult::arbitration_lost : I2CMasterResult::ok;
}

I2CMasterResult readBit(bool& bit) {
  sdaRelease();
  halfPeriod();
  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }
  bit = gpio_get(pins::I2C_SDA);
  halfPeriod();
  sclLow();
  return I2CMasterResult::ok;
}

I2CMasterResult writeByte(uint8_t value) {
  for (int8_t i = 7; i >= 0; i--) {
    const I2CMasterResult result = writeBit(value & (1 << i));
    if (result != I2CMasterResult::ok) {
      return result;
    }
  }
  bool nack;
  const I2CMasterResult result = readBit(nack);
  if (result != I2CMasterResult::ok) {
    return result;
  }
  return nack ? I2CMasterResult::nack : I2CMasterResult::ok;
}

I2CMasterResult readByte(uint8_t& value, bool ack) {
  value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    bool bit;
    const I2CMasterResult result = readBit(bit);
    if (result != I2CMasterResult::ok) {
      return result;
    }
    value = (value << 1) | bit;
  }
  return writeBit(!ack);
}

I2CMasterResult transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  I2CMasterResult result = start(false);
  if (result == I2CMasterResult::ok) {
    result = writeByte(address << 1);
  }
  for (size_t i = 0; i < tx_len && result == I2CMasterResult::ok; i++) {
    result = writeByte(tx[i]);
  }

  if (result == I2CMasterResult::ok && rx_len > 0) {
    result = start(true);
    if (result == I2CMasterResult::ok) {
      result = writeByte((address << 1) | 1);
    }
    for (size_t i = 0; i < rx_len && result == I2CMasterResult::ok; i++) {
      result = readByte(rx[i], i + 1 < rx_len);
    }
  }

  if (result == I2CMasterResult::arbitration_lost || result == I2CMasterResult::bus_busy) {
    // Not our bus right now, just let go
    sdaRelease();
    sclReleaseAndWait();
  } else {
    stop();
  }
  return result;
}

} // namespace

I2CMasterResult i2c_master_transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  if (!busIdle()) {
    return I2CMasterResult::bus_busy;
  }
  takePins();
  const I2CMasterResult result = transfer(address, tx, tx_len, rx, rx_len);
  returnPins();
  return result;
}

// Frees a device stuck holding SDA low: 9 clocks on SCL, then a STOP. The PIC
// does the same when the temperature sensor stops answering (0x49E).
void i2c_bus_recovery() {
  debug::print_message("I2C: Bus recovery");

  takePins();
  for (uint8_t i = 0; i < 9; i++) {
    sclLow();
    halfPeriod();
    sclReleaseAndWait();
    halfPeriod();
  }
  stop();
  returnPins();
}

void init() {
  debug::print_message("GPIO: Init");

  // Idle levels before the pins become outputs, as in the PIC's port_init:
  // LEDs off, SMI released, DVD eject released
  gpio_put(pins::LED_RED, 1);
  gpio_put(pins::LED_GREEN, 1);
  gpio_put(pins::SMI, 1);
  gpio_put(pins::DVD_EJECT, 0); // Open drain, only ever drives low

  gpio_set_function(pins::LED_RED, GPIO_FUNC_SIO);
  gpio_set_dir(pins::LED_RED, GPIO_OUT);
  gpio_set_function(pins::LED_GREEN, GPIO_FUNC_SIO);
  gpio_set_dir(pins::LED_GREEN, GPIO_OUT);

  gpio_set_function(pins::SW_POWER, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_POWER, GPIO_IN);
  gpio_pull_up(pins::SW_POWER);
  gpio_set_function(pins::SW_EJECT, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_EJECT, GPIO_IN);
  gpio_pull_up(pins::SW_EJECT);

  gpio_set_function(pins::SYSTEM_RESET, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SYSTEM_RESET, GPIO_OUT);

  gpio_set_function(pins::SMI, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SMI, GPIO_OUT);

  gpio_set_function(pins::DVD_EJECT, GPIO_FUNC_SIO);
  gpio_disable_pulls(pins::DVD_EJECT);
  dvd_eject_release();

  gpio_set_function(pins::AUDIO_CLAMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::AUDIO_CLAMP, GPIO_OUT);

  gpio_set_function(pins::POWER_OK, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_OK, GPIO_IN);
  gpio_disable_pulls(pins::POWER_OK);

  gpio_set_function(pins::FAN_PWM1, GPIO_FUNC_PWM);
  gpio_set_dir(pins::FAN_PWM1, GPIO_OUT);

  gpio_set_function(pins::POWER_ON, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_ON, GPIO_OUT);

  gpio_set_function(pins::RTC_DUMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::RTC_DUMP, GPIO_OUT);

  gpio_set_function(pins::PLL_ENABLE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::PLL_ENABLE, GPIO_OUT);

  // No internal pulls on the drive, AV and PSU inputs: the PIC has its PORTB
  // pull-ups off (OPTION = 0x8C), and pulling the lines of an unpowered drive
  // up would feed it through its pins
  gpio_set_function(pins::VIDEO_MODE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_0, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_0);
  gpio_set_function(pins::VIDEO_MODE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_1, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_1);
  gpio_set_function(pins::VIDEO_MODE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_2, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_2);

  gpio_set_function(pins::TRAY_STATE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_0, GPIO_IN);
  gpio_disable_pulls(pins::TRAY_STATE_0);
  gpio_set_function(pins::TRAY_STATE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_1, GPIO_IN);
  gpio_disable_pulls(pins::TRAY_STATE_1);
  gpio_set_function(pins::TRAY_STATE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_2, GPIO_IN);
  gpio_disable_pulls(pins::TRAY_STATE_2);

  gpio_set_function(pins::DVD_ACTIVE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::DVD_ACTIVE, GPIO_IN);
  gpio_disable_pulls(pins::DVD_ACTIVE);

  gpio_init(pins::RP2040_LED);
  gpio_set_dir(pins::RP2040_LED, GPIO_OUT);

  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(pins::I2C_SDA);
  gpio_pull_up(pins::I2C_SCL);

  debug::print_message("GPIO: PWM Init");
  pwm_slice = pwm_gpio_to_slice_num(pins::FAN_PWM1);
  pwm_channel = pwm_gpio_to_channel(pins::FAN_PWM1);
  pwm_cfg = pwm_get_default_config();
  pwm_config_set_clkdiv(&pwm_cfg, (float)clock_get_hz(clk_sys) / (FAN_PWM_HZ * (FAN_PWM_WRAP + 1)));
  pwm_config_set_wrap(&pwm_cfg, FAN_PWM_WRAP);
  pwm_init(pwm_slice, &pwm_cfg, false);
  fan_pin_low(); // Same as the PIC's port_init
  pwm_set_enabled(pwm_slice, true);


  debug::print_message("GPIO: I2C Init");
  i2c_init(I2C_PORT, I2C_BAUDRATE);
}

void assertSystemReset() {
  debug::print_message("GPIO: Assert reset");
  gpio_put(pins::SYSTEM_RESET, 0);
}

void liftSystemReset() {
  debug::print_message("GPIO: Lift reset");
  gpio_put(pins::SYSTEM_RESET, 1);
}

void enableWatchdog() {
  debug::print_message("PICO: Watchdog enabled");
  watchdog_enable(1000, 1);
}

void petWatchdog() {
  watchdog_update();
}

bool rebootCauseWatchdog() {
  return watchdog_caused_reboot();
}

// Main loop tick, same as the PIC's Timer1: timer1_init() starts a period,
// timer1_wait() blocks until it has elapsed (returns at once if it already
// has) and starts the next one.
static uint32_t timer1_period_us = TICK_US;
static absolute_time_t timer1_deadline;

void timer1_init(uint32_t delay_us) {
  timer1_period_us = delay_us;
  timer1_deadline = make_timeout_time_us(delay_us);
}

void timer1_wait() {
  busy_wait_until(timer1_deadline);
  timer1_init(timer1_period_us);
}

void disable_and_discard_interrupts() {
  (void) save_and_disable_interrupts();
}

} // namespace hal
