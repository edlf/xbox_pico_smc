#ifndef HAL_HPP
#define HAL_HPP

#include <cstddef>
#include <cstdint>

#include "config.hpp"

namespace hal {
void init();

// Front panel
void led_green_on();
void led_green_off();

void led_red_on();
void led_red_off();

// Front panel LED brightness in percent (1-100), starts at SMC_LED_BRIGHTNESS
void    set_led_brightness(uint8_t percent);
uint8_t get_led_brightness();

// Fade on top of the brightness, 0 (dark) to 255 (full), for power up/down
void set_led_fade(uint8_t level);

bool power_button_pressed();
bool eject_button_pressed();

void pico_led_on();
void pico_led_off();

// 0 (off) to 255 (full), Pico and Pico 2 only
void set_pico_led_level(uint8_t level);

// Raw 12-bit ADC readings (0-4095 of 3.3 V), averaged: the chip's temperature
// sensor, and the NTC divider on pins::NTC_ADC (only set up with SMC_NTC_SENSOR)
uint16_t read_pico_temperature_adc();
uint16_t read_ntc_adc();

void smi_pin_on();
void smi_pin_off();

void audio_clamp_on();
void audio_clamp_off();

void turn_on_psu();
void turn_off_psu();

void PLL_on();
void PLL_off();

void dvd_eject_assert();
void dvd_eject_release();

void rtc_dump_on();
void rtc_dump_off();

uint8_t get_tray_state();

// PSU
bool get_power_ok();

// AV port connector
uint8_t get_video_mode();

// FAN
constexpr uint8_t FAN_MIN_SPEED = 10;  // The fan is never stopped while the Xbox is on
constexpr uint8_t FAN_MAX_SPEED = 50;
void              set_fan_speed(uint8_t speed);  // 10-50, same scale as SMBus 0x10
void              fan_pin_low();                 // Pin state while the Xbox is off

// SMBus master transfer (write tx, then read rx after a repeated START),
// bit-banged on the SMBus slave's pins like the PIC
enum class I2CMasterResult { ok, bus_busy, nack, arbitration_lost, timeout };
I2CMasterResult i2c_master_transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len);
void            i2c_bus_recovery();

// Interface
void assertSystemReset();
void liftSystemReset();

// SYSRESET held low (the Xbox is in reset)
bool system_reset_asserted();

// WS2812 RGB LED on pins::RGB_LED (SMC_RGB_LED), 0-255 per colour, scaled by
// the LED brightness. Only sent when it changes.
void rgb_led_init();
void set_rgb_led(uint8_t red, uint8_t green, uint8_t blue);

void enableWatchdog();
void petWatchdog();

// Original SMC main loop tick: Timer1 preload 0xE795, 1:8 @ 10 MHz = 20 ms
constexpr uint32_t TICK_US = 20000;

void timer1_init(uint32_t delay_us);
void timer1_wait();

void disable_and_discard_interrupts();

}  // namespace hal

#endif  // HAL_HPP
