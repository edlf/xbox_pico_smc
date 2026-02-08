#include "hardware/i2c.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "i2c_slave/include/i2c_slave.h"
#include "pico/stdlib.h"
#include <stdio.h>

#include "pico_hal.hpp"
#include "pin_assignments.hpp"

namespace pico_hal {
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

void system_reset_on() {
  gpio_put(pins::SYSTEM_RESET, 1);
}

void system_reset_off() {
  gpio_put(pins::SYSTEM_RESET, 0);
}

void PLL_on() {
  gpio_put(pins::PLL_ENABLE, 1);
}

void PLL_off() {
  gpio_put(pins::PLL_ENABLE, 0);
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

void power_on_assert() {
  printf("Assert POWER ON\n");
  gpio_put(pins::POWER_ON, 1);
}

void power_on_deassert() {
  printf("Deassert POWER ON\n");
  gpio_put(pins::POWER_ON, 0);
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
  return tray_state;
}

bool power_button_pressed() {
  return gpio_get(pins::SW_POWER);
}

bool eject_button_pressed() {
  return gpio_get(pins::SW_EJECT);
}

void set_fan_on() {
  // TODO
}

void set_fan_off() {
  // TODO
}

void set_fan_speed(uint16_t level) {
  pwm_set_gpio_level(pins::FAN_PWM1, level);
}

bool read_byte(const uint8_t device, const uint8_t reg, uint8_t* out) {
  if (out != nullptr) {
    *out = 99;
    return true;
  }

  return false;
}

bool smbus_reply(const uint8_t data) {
  return false;
}

bool smbus_send_byte(const uint8_t device, const uint8_t data) {
  return false;
}

void init() {
  printf("Init GPIO\n");
  gpio_set_function(pins::LED_RED, GPIO_FUNC_SIO);
  gpio_set_dir(pins::LED_RED, GPIO_OUT);
  gpio_set_function(pins::LED_GREEN, GPIO_FUNC_SIO);
  gpio_set_dir(pins::LED_GREEN, GPIO_OUT);

  gpio_set_function(pins::SW_POWER, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_POWER, GPIO_IN);
  gpio_set_function(pins::SW_EJECT, GPIO_FUNC_SIO);
  gpio_set_dir(pins::SW_EJECT, GPIO_IN);

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
  // uint slice = pwm_gpio_to_slice_num(pins::FAN_PWM1);
  // uint channel = pwm_gpio_to_channel(pins::FAN_PWM1);
  // pwm_set_enabled(slice, true);

  gpio_set_function(pins::POWER_ON, GPIO_FUNC_SIO);
  gpio_set_dir(pins::POWER_ON, GPIO_OUT);

  gpio_set_function(pins::RTC_DUMP, GPIO_FUNC_SIO);
  gpio_set_dir(pins::RTC_DUMP, GPIO_OUT);

  gpio_set_function(pins::PLL_ENABLE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::PLL_ENABLE, GPIO_OUT);

  gpio_set_function(pins::VIDEO_MODE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_0, GPIO_IN);
  gpio_set_function(pins::VIDEO_MODE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_1, GPIO_IN);
  gpio_set_function(pins::VIDEO_MODE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::VIDEO_MODE_2, GPIO_IN);

  gpio_set_function(pins::TRAY_STATE_0, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_0, GPIO_IN);
  gpio_set_function(pins::TRAY_STATE_1, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_1, GPIO_IN);
  gpio_set_function(pins::TRAY_STATE_2, GPIO_FUNC_SIO);
  gpio_set_dir(pins::TRAY_STATE_2, GPIO_IN);

  gpio_set_function(pins::DVD_ACTIVE, GPIO_FUNC_SIO);
  gpio_set_dir(pins::DVD_ACTIVE, GPIO_IN);

  gpio_init(pins::RP2040_LED);
  gpio_set_dir(pins::RP2040_LED, GPIO_OUT);

  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(pins::I2C_SDA);
  gpio_pull_up(pins::I2C_SCL);

  i2c_init(i2c0, I2C_BAUDRATE);
}

void assertSystemReset() {
  system_reset_off();
}

void liftSystemReset() {
  system_reset_on();
}

void fireSystemInterrupt() {
  /* Pulse SMI signal (RA4) low to signal system interrupt */
  smi_pin_off();
  smi_pin_on();
}

void configureConexantEncoder() {
  smbus_send_byte(0x8A, 0xBA);
  smbus_send_byte(0x8A, 0x3F);
}

void enableWatchdog() {
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

  // printf("timer0 fired\n");
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
  // printf("timer1 fired\n");
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

void panic(const char* message) {
  timer0_disable();
  timer1_disable();
  printf(message);
  while (true) {
    gpio_put(pins::RP2040_LED, 1);
    sleep_ms(500);
    gpio_put(pins::RP2040_LED, 0);
  }
}

} // namespace pico_hal
