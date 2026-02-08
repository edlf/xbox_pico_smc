#ifndef __PIN_ASSIGNMENTS__
#define __PIN_ASSIGNMENTS__

#include "hardware/i2c.h"

namespace pins {
// SMC
constexpr uint8_t LED_RED      = 2;
constexpr uint8_t LED_GREEN    = 3;
constexpr uint8_t SYSTEM_RESET = 4;
constexpr uint8_t SW_EJECT     = 5;
constexpr uint8_t SMI          = 6;
constexpr uint8_t DVD_EJECT    = 7;
constexpr uint8_t AUDIO_CLAMP  = 8;
constexpr uint8_t POWER_OK     = 9;
constexpr uint8_t FAN_PWM1     = 10;
// 14-28
constexpr uint8_t I2C_SDA      = 12;
constexpr uint8_t I2C_SCL      = 13;
constexpr uint8_t POWER_ON     = 11;
constexpr uint8_t RTC_DUMP     = 14;
constexpr uint8_t SW_POWER     = 15;
constexpr uint8_t PLL_ENABLE   = 16;
constexpr uint8_t VIDEO_MODE_0 = 17;
constexpr uint8_t VIDEO_MODE_1 = 18;
constexpr uint8_t VIDEO_MODE_2 = 19;
constexpr uint8_t TRAY_STATE_0 = 20;
constexpr uint8_t TRAY_STATE_1 = 21;
constexpr uint8_t TRAY_STATE_2 = 22;
constexpr uint8_t DVD_ACTIVE   = 26;

// Pico Internal
constexpr uint8_t RP2040_LED = 25;
// RGB Led
constexpr uint8_t YD_BOARD_RGB = 23;
}  // namespace pins

#define I2C_PORT i2c0
constexpr uint32_t I2C_BAUDRATE = 100000U;  // 100 kHz
constexpr uint8_t I2C_SLAVE_ADDRESS = 0x20;
constexpr uint8_t I2C_TEMP_SENSOR_ADDRESS = 0x98;

#endif  // __PIN_ASSIGNMENTS__
