#ifndef __PICO_HAL__
#define __PICO_HAL__

#include <cstdint>
#include <string>

namespace pico_hal {
void init();

// Front panel
void led_green_on();
void led_green_off();
void led_red_on();
void led_red_off();
bool power_button_pressed();
bool eject_button_pressed();

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
void dvd_eject_on();
void dvd_eject_off();

uint8_t get_tray_state();

// PSU
bool get_power_ok();

// AV port connector
uint8_t get_video_mode();

// FAN
void set_fan_on();
void set_fan_off();
void set_fan_pwm(uint8_t level);

// Interface
void assertSystemReset();
void liftSystemReset();
void enableWatchdog();
void petWatchdog();
bool rebootCauseWatchdog();
uint32_t disableInterrupts();
void reenableInterrupts(uint32_t status);
void timer0_init(uint32_t delay_us);
void timer0_wait();
void timer0_disable();
void timer1_init(uint32_t delay_us);
void timer1_wait();
void timer1_disable();

// I2C
void setupI2C();

void shutdown_xbox();

//
void panic(const std::string message);
} // namespace pico_hal

#endif // __GPIO__
