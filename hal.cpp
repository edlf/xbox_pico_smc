#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "i2c_slave/include/i2c_slave.h"
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

void led_green_on() {
  gpio_put(pins::LED_GREEN, 1);
}

void led_green_off() {
  gpio_put(pins::LED_GREEN, 0);
}

void led_red_on() {
  gpio_put(pins::LED_RED, 1);
}

void led_red_off() {
  gpio_put(pins::LED_RED, 0);
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

void dvd_eject_on() {
  gpio_put(pins::DVD_EJECT, 1);
}

void dvd_eject_off() {
  gpio_put(pins::DVD_EJECT, 0);
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

bool state_fan = false;

void set_fan_on() {
  pwm_set_chan_level(pwm_slice, PWM_CHAN_A, 20);
  pwm_set_enabled(pwm_slice, true);

  if (!state_fan) {
    debug::print_message("GPIO: Fan ON");
    state_fan = true;
  }
}

void set_fan_off() {
  pwm_set_chan_level(pwm_slice, PWM_CHAN_A, 0);
  pwm_set_enabled(pwm_slice, false);

  if (state_fan) {
    debug::print_message("GPIO: Fan OFF");
    state_fan = false;
  }
}

void set_fan_pwm(uint8_t level) {
  debug::print_message("GPIO: Set Fan level: " + std::to_string(level));
  pwm_set_gpio_level(pins::FAN_PWM1, level);
}

void init() {
  debug::print_message("GPIO: Init");

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
  gpio_set_dir(pins::DVD_EJECT, GPIO_OUT);

  gpio_set_function(pins::AUDIO_CLAMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::AUDIO_CLAMP, GPIO_OUT);

  gpio_set_function(pins::POWER_OK, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_OK, GPIO_IN);

  gpio_set_function(pins::FAN_PWM1, GPIO_FUNC_PWM);
  gpio_set_dir(pins::FAN_PWM1, GPIO_OUT);

  gpio_set_function(pins::POWER_ON, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_ON, GPIO_OUT);

  gpio_set_function(pins::RTC_DUMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::RTC_DUMP, GPIO_OUT);

  gpio_set_function(pins::PLL_ENABLE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::PLL_ENABLE, GPIO_OUT);

  gpio_set_function(pins::VIDEO_MODE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_0, GPIO_IN);
  gpio_pull_up(pins::VIDEO_MODE_0);
  gpio_set_function(pins::VIDEO_MODE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_1, GPIO_IN);
  gpio_pull_up(pins::VIDEO_MODE_1);
  gpio_set_function(pins::VIDEO_MODE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_2, GPIO_IN);
  gpio_pull_up(pins::VIDEO_MODE_2);

  gpio_set_function(pins::TRAY_STATE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_0, GPIO_IN);
  gpio_pull_up(pins::TRAY_STATE_0);
  gpio_set_function(pins::TRAY_STATE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_1, GPIO_IN);
  gpio_pull_up(pins::TRAY_STATE_1);
  gpio_set_function(pins::TRAY_STATE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_2, GPIO_IN);
  gpio_pull_up(pins::TRAY_STATE_2);

  gpio_set_function(pins::DVD_ACTIVE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::DVD_ACTIVE, GPIO_IN);

  gpio_init(pins::RP2040_LED);
  gpio_set_dir(pins::RP2040_LED, GPIO_OUT);

  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(pins::I2C_SDA);
  gpio_pull_up(pins::I2C_SCL);

  debug::print_message("GPIO: PWM Init");
  pwm_slice = pwm_gpio_to_slice_num(pins::FAN_PWM1);
  pwm_channel = pwm_gpio_to_channel(pins::FAN_PWM1);
  pwm_init(pwm_slice, &pwm_cfg, true);
  pwm_set_wrap(pwm_slice, 50);
  pwm_set_chan_level(pwm_slice, PWM_CHAN_A, 0);
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

uint32_t disableInterrupts() {
  uint32_t status;
  __asm volatile("mrs %0, PRIMASK" : "=r"(status)::);
  __asm volatile("cpsid i");
  return status;
}

void reenableInterrupts(uint32_t status) {
  __asm volatile("msr PRIMASK,%0" ::"r"(status) :);
}

// TODO use sdk instead
static uint64_t get_time(void) {
  // Reading low latches the high value
  uint32_t lo = timer_hw->timelr;
  uint32_t hi = timer_hw->timehr;
  return ((uint64_t)hi << 32u) | lo;
}

#define ALARM0_IRQ timer_hardware_alarm_get_irq_num(timer_hw, 0)
#define ALARM1_IRQ timer_hardware_alarm_get_irq_num(timer_hw, 1)

volatile bool timer0_fired;

void timer0_callback() {
  timer0_disable();

  // debug::print_message("timer0 fired");
  timer0_fired = true;
}

void timer0_init(uint32_t delay_us) {
  timer0_fired = false;

  const uint8_t alarm_no = 0;

  // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
  hw_set_bits(&timer_hw->inte, 1u << alarm_no);
  // Set irq handler for alarm irq
  irq_set_exclusive_handler(ALARM0_IRQ, timer0_callback);
  // Enable the alarm irq
  irq_set_enabled(ALARM0_IRQ, true);
  // Enable interrupt in block and at processor

  // Alarm is only 32 bits so if trying to delay more
  // than that need to be careful and keep track of the upper
  // bits
  uint64_t target = timer_hw->timerawl + delay_us;

  // Write the lower 32 bits of the target time to the alarm which
  // will arm it
  timer_hw->alarm[alarm_no] = (uint32_t)target;
}

void timer0_wait() {
  while (!timer0_fired)
    ;
  timer0_fired = false;
}

bool timer1_fired;

void timer1_callback() {
  timer1_disable();
  // debug::print_message("timer1 fired");
  timer1_fired = true;
}

void timer1_init(uint32_t delay_us) {
  timer0_fired = false;

  const uint8_t alarm_no = 1;

  // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
  hw_set_bits(&timer_hw->inte, 1u << alarm_no);
  // Set irq handler for alarm irq
  irq_set_exclusive_handler(ALARM1_IRQ, timer1_callback);
  // Enable the alarm irq
  irq_set_enabled(ALARM1_IRQ, true);
  // Enable interrupt in block and at processor

  // Alarm is only 32 bits so if trying to delay more
  // than that need to be careful and keep track of the upper
  // bits
  uint64_t target = timer_hw->timerawl + delay_us;

  // Write the lower 32 bits of the target time to the alarm which
  // will arm it
  timer_hw->alarm[alarm_no] = (uint32_t)target;
}

void timer1_wait() {
  while (!timer1_fired)
    ;
  timer1_fired = false;
}

void timer0_disable() {
  hw_clear_bits(&timer_hw->intr, 1u << 0);
}

void timer1_disable() {
  hw_clear_bits(&timer_hw->intr, 1u << 1);
}

// I2C
void setupI2C() {
  // 0x36
}

uint32_t get_ms_since_boot() {
  return to_ms_since_boot(get_absolute_time());
}

void disable_and_discard_interrupts() {
  (void) save_and_disable_interrupts();
}

} // namespace hal
