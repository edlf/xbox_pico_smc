#ifndef __HAL__
#define __HAL__

#include <cstddef>
#include <cstdint>
#include <string>

// Set by CMake (option SMC_LED_BRIGHTNESS): front panel LED brightness in percent
#ifndef SMC_LED_BRIGHTNESS
#define SMC_LED_BRIGHTNESS 100
#endif

// Set by CMake (option SMC_LED_RED_BREATHE): red LED breathes while on
#ifndef SMC_LED_RED_BREATHE
#define SMC_LED_RED_BREATHE 0
#endif

namespace hal {
void init();

// Front panel
void led_green_on();
void led_green_off();
void led_red_on();
void led_red_off();
bool power_button_pressed();
bool eject_button_pressed();
void pico_led_on();
void pico_led_off();

void system_reset_on();
void system_reset_off();
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
constexpr uint8_t FAN_MAX_SPEED = 50;
void set_fan_speed(uint8_t speed); // 0-50, same scale as SMBus 0x10
void fan_pin_low();                // Pin state while the Xbox is off
// SMBus master transfer (write tx, then read rx after a repeated START),
// bit-banged on the SMBus slave's pins like the PIC
enum class I2CMasterResult { ok, bus_busy, nack, arbitration_lost, timeout };
I2CMasterResult i2c_master_transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len);
void i2c_bus_recovery();

// Interface
void assertSystemReset();
void liftSystemReset();
void enableWatchdog();
void petWatchdog();
bool rebootCauseWatchdog();
// Original SMC main loop tick: Timer1 preload 0xE795, 1:8 @ 10 MHz = 20 ms
constexpr uint32_t TICK_US = 20000;
void timer1_init(uint32_t delay_us);
void timer1_wait();
void disable_and_discard_interrupts();

} // namespace hal

#endif // __HAL__
