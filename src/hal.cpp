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
#include "hal_internal.hpp"

namespace hal {

// Fan PWM output, see set_fan_speed()
static uint pwm_slice;
static uint pwm_channel;

static bool state_pll = false;

void PLL_on() {
  gpio_put(pins::PLL_ENABLE, 1);

  if (!state_pll) {
    debug::print(debug::Level::Info, "GPIO: Assert PLL");
    state_pll = true;
  }
}

void PLL_off() {
  gpio_put(pins::PLL_ENABLE, 0);

  if (state_pll) {
    debug::print(debug::Level::Info, "GPIO: Deassert PLL");
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

static bool state_psu = false;

void turn_on_psu() {
  gpio_put(pins::POWER_ON, 1);

  if (!state_psu) {
    debug::print(debug::Level::Info, "GPIO: Turn on xbox PSU");
    state_psu = true;
  }
}

void turn_off_psu() {
  gpio_put(pins::POWER_ON, 0);

  if (state_psu) {
    debug::print(debug::Level::Info, "GPIO: Turn off xbox PSU");
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

// RP2350-E9: while a pad's input buffer is on it leaks current that pulls a
// weakly driven pin up to about 2.2 V. The front panel buttons only pull their
// line down weakly (a press measured 1.6 V), and AV mode 1 is pulled up to
// POWON, so in standby only that unpowered rail holds it low and it read high
// (no cable, which powers the Xbox on). As the erratum suggests, the buffer is
// only switched on for the read.
static bool readWeakInput(const uint8_t pin) {
#if PICO_RP2350
  gpio_set_input_enabled(pin, true);
  busy_wait_us(10);
  const bool level = gpio_get(pin);
  gpio_set_input_enabled(pin, false);
  return level;
#else
  return gpio_get(pin);
#endif
}

// AV pack ID pins, as PORTB & 0x0E on the PIC (av_pack)
uint8_t get_video_mode() {
  uint8_t video_mode = 0;

  if (readWeakInput(pins::VIDEO_MODE_0)) {
    video_mode = video_mode | (1 << 1);
  }
  if (readWeakInput(pins::VIDEO_MODE_1)) {
    video_mode = video_mode | (1 << 2);
  }
  if (readWeakInput(pins::VIDEO_MODE_2)) {
    video_mode = video_mode | (1 << 3);
  }

  return video_mode;
}

// DVD drive tray state pins, as PORTB & 0x70 on the PIC (drive_tray_state)
uint8_t get_tray_state() {
  uint8_t tray_state = 0;

  if (gpio_get(pins::TRAY_STATE_0)) {
    tray_state = tray_state | (1 << 4);
  }
  if (gpio_get(pins::TRAY_STATE_1)) {
    tray_state = tray_state | (1 << 5);
  }
  if (gpio_get(pins::TRAY_STATE_2)) {
    tray_state = tray_state | (1 << 6);
  }

  return tray_state;
}

// The buttons pull their line low
bool power_button_pressed() {
  return !readWeakInput(pins::SW_POWER);
}

bool eject_button_pressed() {
  return !readWeakInput(pins::SW_EJECT);
}

// Fan PWM, same output as the PIC's CCP1: ~9.8 kHz, 10-bit, and
// inverted: 50 holds the pin low, and lower speeds keep it high for
// (50 - speed)% of each period. The PIC also has speed 0 (pin held high, fan
// stopped); here the speed never goes below FAN_MIN_SPEED.
constexpr uint16_t FAN_PWM_WRAP = 1023;
constexpr uint32_t FAN_PWM_HZ = 9766;  // PIC: 10 MHz / 4 / 256 (PR2 = 0xFF)

static uint8_t fan_speed_set = 0xFF;

void set_fan_speed(uint8_t speed) {
  if (speed < FAN_MIN_SPEED) {
    speed = FAN_MIN_SPEED;
  } else if (speed > FAN_MAX_SPEED) {
    speed = FAN_MAX_SPEED;
  }

  const uint16_t level = static_cast<uint16_t>(static_cast<uint32_t>(FAN_PWM_WRAP + 1) * (FAN_MAX_SPEED - speed) / (2 * FAN_MAX_SPEED));
  pwm_set_chan_level(pwm_slice, pwm_channel, level);

  if (speed != fan_speed_set) {
    debug::print(debug::Level::Debug, "GPIO: Fan speed %u", speed);  // Fan::setSpeed() logs the change (INFO)
    fan_speed_set = speed;
  }
}

void fan_pin_low() {
  pwm_set_chan_level(pwm_slice, pwm_channel, 0);
  fan_speed_set = 0xFF;
}

void init() {
  debug::print(debug::Level::Info, "GPIO: Init");

  init_leds();
  init_adc();
  rgb_led_init();

  // Idle levels before the pins become outputs, as in the PIC's port_init:
  // SMI released, DVD eject released
  gpio_put(pins::SMI, 1);
  gpio_put(pins::DVD_EJECT, 0);  // Open drain, only ever drives low

  gpio_set_function(pins::SW_POWER, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_POWER, GPIO_IN);
  gpio_pull_up(pins::SW_POWER);

  gpio_set_function(pins::SW_EJECT, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_EJECT, GPIO_IN);
  gpio_pull_up(pins::SW_EJECT);

#if PICO_RP2350
  // Input buffers only on while reading, see readWeakInput()
  gpio_set_input_enabled(pins::SW_POWER, false);
  gpio_set_input_enabled(pins::SW_EJECT, false);
#endif

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

  // No internal pulls on the drive, AV and PSU inputs
  gpio_set_function(pins::VIDEO_MODE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_0, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_0);
  gpio_set_function(pins::VIDEO_MODE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_1, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_1);
  gpio_set_function(pins::VIDEO_MODE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_2, GPIO_IN);
  gpio_disable_pulls(pins::VIDEO_MODE_2);

#if PICO_RP2350
  // Input buffers only on while reading, see readWeakInput()
  gpio_set_input_enabled(pins::VIDEO_MODE_0, false);
  gpio_set_input_enabled(pins::VIDEO_MODE_1, false);
  gpio_set_input_enabled(pins::VIDEO_MODE_2, false);
#endif

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

  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(pins::I2C_SDA);
  gpio_pull_up(pins::I2C_SCL);

  debug::print(debug::Level::Info, "GPIO: PWM Init");
  pwm_slice = pwm_gpio_to_slice_num(pins::FAN_PWM1);
  pwm_channel = pwm_gpio_to_channel(pins::FAN_PWM1);
  pwm_config pwm_cfg = pwm_get_default_config();
  pwm_config_set_clkdiv(&pwm_cfg, static_cast<float>(clock_get_hz(clk_sys)) / (FAN_PWM_HZ * (FAN_PWM_WRAP + 1)));
  pwm_config_set_wrap(&pwm_cfg, FAN_PWM_WRAP);
  pwm_init(pwm_slice, &pwm_cfg, false);
  fan_pin_low();
  pwm_set_enabled(pwm_slice, true);

  debug::print(debug::Level::Info, "GPIO: I2C Init");
  i2c_init(I2C_PORT, I2C_BAUDRATE);
}

void assertSystemReset() {
  debug::print(debug::Level::Info, "GPIO: Assert reset");
  gpio_put(pins::SYSTEM_RESET, 0);
}

void liftSystemReset() {
  debug::print(debug::Level::Info, "GPIO: Lift reset");
  gpio_put(pins::SYSTEM_RESET, 1);
}

bool system_reset_asserted() {
  return !gpio_get_out_level(pins::SYSTEM_RESET);
}

void enableWatchdog() {
  debug::print(debug::Level::Info, "PICO: Watchdog enabled");
  watchdog_enable(1000, 1);
}

void petWatchdog() {
  watchdog_update();
}

// Main loop tick, same as the PIC's Timer1: timer1_init() starts a period,
// timer1_wait() blocks until it has elapsed (returns at once if it already
// has) and starts the next one. It sleeps (WFE on a timer alarm) rather than
// spinning; interrupts (SMBus, POWER_OK, USB) still run while it waits.
static uint32_t        timer1_period_us = TICK_US;
static absolute_time_t timer1_deadline;

void timer1_init(uint32_t delay_us) {
  timer1_period_us = delay_us;
  timer1_deadline = make_timeout_time_us(delay_us);
}

void timer1_wait() {
  sleep_until(timer1_deadline);
  timer1_init(timer1_period_us);
}

void disable_and_discard_interrupts() {
  (void)save_and_disable_interrupts();
}

}  // namespace hal
