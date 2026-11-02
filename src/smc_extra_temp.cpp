#include "smc_extra_temp.hpp"
#include "smc.hpp"
#include "hal.hpp"

#include <cmath>
#include <cstdio>

namespace SMC {
namespace ExtraTemp {

sensors_struct sensors;

constexpr uint8_t sample_ticks = 50;  // 1 s
constexpr float   adc_vref = 3.3f;
constexpr float   adc_full_scale = 4096.0f;
// Readings this close to either end are an open or shorted NTC
constexpr uint16_t ntc_limit = 16;

static uint8_t tick_counter = 0;
static int16_t logged_pico = INT16_MIN;  // Whole °C last logged
static int16_t logged_ntc = INT16_MIN;

// RP2040/RP2350 datasheet: 0.706 V at 27 °C, -1.721 mV per °C
static int16_t picoTemperature(const uint16_t raw) {
  const float volts = raw * adc_vref / adc_full_scale;
  return (int16_t)lroundf((27.0f - (volts - 0.706f) / 0.001721f) * 10.0f);
}

// NTC from the pin to ground, SMC_NTC_SERIES_R from 3.3 V to the pin. Beta
// equation from the resistance at 25 °C.
static int16_t ntcTemperature(const uint16_t raw) {
  const float resistance = (float)SMC_NTC_SERIES_R * raw / (adc_full_scale - raw);
  const float kelvin = 1.0f / (1.0f / 298.15f + logf(resistance / SMC_NTC_R25) / SMC_NTC_BETA);
  return (int16_t)lroundf((kelvin - 273.15f) * 10.0f);
}

static void logChange(const char* name, const int16_t tenths, int16_t& logged) {
  const int16_t whole = tenths / 10;
  if (whole == logged) {
    return;
  }

  logged = whole;
  char      msg[48];
  const int magnitude = std::abs(tenths);
  snprintf(msg, sizeof(msg), "TEMP: %s %s%d.%d C", name, tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
  debug::print_debug(msg);
}

static void sample() {
  sensors.pico_temperature = picoTemperature(hal::read_pico_temperature_adc());
  logChange("Pico", sensors.pico_temperature, logged_pico);

  if (!SMC_NTC_SENSOR) {
    return;
  }

  const uint16_t raw = hal::read_ntc_adc();
  const bool     connected = raw > ntc_limit && raw < adc_full_scale - ntc_limit;
  if (connected != sensors.ntc_connected) {
    debug::print_message(connected ? "TEMP: NTC connected" : "TEMP: NTC open or shorted");
    logged_ntc = INT16_MIN;
  }

  sensors.ntc_connected = connected;
  if (connected) {
    sensors.ntc_temperature = ntcTemperature(raw);
    logChange("NTC", sensors.ntc_temperature, logged_ntc);
  }
}

void init() {
  sensors = {};
  tick_counter = 0;
  sample();  // Valid from the start
}

void update() {
  if (++tick_counter < sample_ticks) {
    return;
  }

  tick_counter = 0;
  sample();
}

}  // namespace ExtraTemp
}  // namespace SMC
