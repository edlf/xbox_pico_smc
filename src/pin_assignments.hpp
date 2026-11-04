#ifndef PIN_ASSIGNMENTS_HPP
#define PIN_ASSIGNMENTS_HPP

#include <cstdint>
#include "hardware/i2c.h"

// GPIO numbers, see docs/Wiring.md for the PIC pins they replace
namespace pins {
constexpr uint8_t LED_RED = 2;
constexpr uint8_t LED_GREEN = 3;
constexpr uint8_t SYSTEM_RESET = 4;
constexpr uint8_t SW_EJECT = 5;
constexpr uint8_t SMI = 6;
constexpr uint8_t DVD_EJECT = 7;
constexpr uint8_t AUDIO_CLAMP = 8;
constexpr uint8_t POWER_OK = 9;
constexpr uint8_t FAN_PWM1 = 10;
constexpr uint8_t POWER_ON = 11;
constexpr uint8_t I2C_SDA = 12;
constexpr uint8_t I2C_SCL = 13;
constexpr uint8_t RTC_DUMP = 14;
constexpr uint8_t SW_POWER = 15;
constexpr uint8_t PLL_ENABLE = 16;
constexpr uint8_t VIDEO_MODE_0 = 17;
constexpr uint8_t VIDEO_MODE_1 = 18;
constexpr uint8_t VIDEO_MODE_2 = 19;
constexpr uint8_t TRAY_STATE_0 = 20;
constexpr uint8_t TRAY_STATE_1 = 21;
constexpr uint8_t TRAY_STATE_2 = 22;
constexpr uint8_t DVD_ACTIVE = 26;
// WS2812 RGB LED (SMC_RGB_LED), shows the boot phases
constexpr uint8_t RGB_LED = 27;
// Optional NTC thermistor (SMC_NTC_SENSOR), ADC2
constexpr uint8_t NTC_ADC = 28;

// Pico onboard LED: a GPIO on the Pico and Pico 2. On the W boards it's on the
// wireless chip (and GPIO23-25 belong to that chip), so it isn't used there.
#ifdef PICO_DEFAULT_LED_PIN
constexpr uint8_t PICO_LED = PICO_DEFAULT_LED_PIN;
#endif
}  // namespace pins

#define I2C_PORT i2c0

constexpr uint32_t I2C_BAUDRATE = 100000U;  // 100 kHz
constexpr uint8_t  I2C_SLAVE_ADDRESS = 0x10;
constexpr uint8_t  I2C_TEMP_SENSOR_ADDRESS = 0x4C;
constexpr uint8_t  I2C_CONEXANT_ADDRESS = 0x45;

#endif  // PIN_ASSIGNMENTS_HPP
