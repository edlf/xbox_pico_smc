#ifndef HAL_INTERNAL_HPP
#define HAL_INTERNAL_HPP

// Set up of the HAL's parts, called by hal::init() only

namespace hal {

void init_leds();  // Front panel and Pico LEDs, hal_leds.cpp (the RGB LED has rgb_led_init())
void init_adc();   // Temperature sensor and NTC, hal_adc.cpp

}  // namespace hal

#endif  // HAL_INTERNAL_HPP
