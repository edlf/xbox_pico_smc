#ifndef FAKES_HPP
#define FAKES_HPP

// What the fake HAL and the fake debug log hold, for the tests to set and
// check. reset() puts everything back to an Xbox that's off with no inputs.

#include <cstdint>
#include <string>
#include <vector>

namespace fake {

struct Hal {
  // Inputs, set by the tests
  bool     power_button = false;
  bool     eject_button = false;
  bool     power_ok = false;
  uint8_t  tray_state = 0x00;           // PORTB & 0x70 encoding
  uint8_t  video_mode = 0x00;           // PORTB & 0x0E encoding
  uint16_t pico_temperature_adc = 876;  // About 27 °C
  uint16_t ntc_adc = 0;

  // Outputs, checked by the tests
  bool    green_led = false;
  bool    red_led = false;
  uint8_t led_brightness = 100;
  bool    psu_on = false;
  bool    pll_on = false;
  bool    system_reset = true;  // Asserted
  bool    audio_clamped = false;
  bool    dvd_eject = false;
  bool    rtc_dump = false;
  uint8_t fan_speed = 0;  // 0: pin low
  int     smi_pulses = 0;

  // SMBus master transfers (temperature sensor, encoder)
  struct Transfer {
    uint8_t              address;
    std::vector<uint8_t> tx;
  };
  std::vector<Transfer> transfers;
  uint8_t               cpu_temperature = 40;    // Answer to a read of ADM1032 register 0x01
  uint8_t               board_temperature = 30;  // Register 0x00
};

extern Hal hal;

// Log lines (level tag + message), newest last
extern std::vector<std::string> log;
bool                            logged(const std::string& text);  // A line containing text was logged

void reset();

}  // namespace fake

#endif  // FAKES_HPP
