// hal:: implemented on plain variables (fakes.hpp), so the state machines run
// on the PC

#include "fakes.hpp"
#include "hal.hpp"

namespace fake {

Hal                      hal;
std::vector<std::string> log;

bool logged(const std::string& text) {
  for (const std::string& line : log) {
    if (line.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void reset() {
  hal = Hal{};
  log.clear();
}

}  // namespace fake

namespace hal {

void init() {}

void led_green_on() {
  fake::hal.green_led = true;
}
void led_green_off() {
  fake::hal.green_led = false;
}
void led_red_on() {
  fake::hal.red_led = true;
}
void led_red_off() {
  fake::hal.red_led = false;
}

void set_led_brightness(uint8_t percent) {
  fake::hal.led_brightness = percent < 1 ? 1 : (percent > 100 ? 100 : percent);
}
uint8_t get_led_brightness() {
  return fake::hal.led_brightness;
}
void set_led_fade(uint8_t) {}

bool power_button_pressed() {
  return fake::hal.power_button;
}
bool eject_button_pressed() {
  return fake::hal.eject_button;
}

void pico_led_on() {}
void pico_led_off() {}
void set_pico_led_level(uint8_t) {}

uint16_t read_pico_temperature_adc() {
  return fake::hal.pico_temperature_adc;
}
uint16_t read_ntc_adc() {
  return fake::hal.ntc_adc;
}

void smi_pin_on() {}
void smi_pin_off() {
  fake::hal.smi_pulses++;
}
void audio_clamp_on() {
  fake::hal.audio_clamped = true;
}
void audio_clamp_off() {
  fake::hal.audio_clamped = false;
}
void turn_on_psu() {
  fake::hal.psu_on = true;
}
void turn_off_psu() {
  fake::hal.psu_on = false;
}
void PLL_on() {
  fake::hal.pll_on = true;
}
void PLL_off() {
  fake::hal.pll_on = false;
}
void dvd_eject_assert() {
  fake::hal.dvd_eject = true;
}
void dvd_eject_release() {
  fake::hal.dvd_eject = false;
}
void rtc_dump_on() {
  fake::hal.rtc_dump = true;
}
void rtc_dump_off() {
  fake::hal.rtc_dump = false;
}

uint8_t get_tray_state() {
  return fake::hal.tray_state;
}
bool get_power_ok() {
  return fake::hal.power_ok;
}
uint8_t get_video_mode() {
  return fake::hal.video_mode;
}

void set_fan_speed(uint8_t speed) {
  fake::hal.fan_speed = speed;
}
void fan_pin_low() {
  fake::hal.fan_speed = 0;
}

I2CMasterResult i2c_master_transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  fake::hal.transfers.push_back({address, std::vector<uint8_t>(tx, tx + tx_len)});
  if (rx_len == 1 && tx_len == 1) {
    rx[0] = tx[0] == 0x01 ? fake::hal.cpu_temperature : fake::hal.board_temperature;
  }
  return I2CMasterResult::ok;
}
void i2c_bus_recovery() {}

void assertSystemReset() {
  fake::hal.system_reset = true;
}
void liftSystemReset() {
  fake::hal.system_reset = false;
}
bool system_reset_asserted() {
  return fake::hal.system_reset;
}

void rgb_led_init() {}
void set_rgb_led(uint8_t, uint8_t, uint8_t) {}

void enableWatchdog() {}
void petWatchdog() {}

void timer1_init(uint32_t) {}
void timer1_wait() {}
void disable_and_discard_interrupts() {}

}  // namespace hal
