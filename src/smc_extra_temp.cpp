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

static NtcConfig ntc = {SMC_NTC_SENSOR != 0, SMC_NTC_BETA, SMC_NTC_R25, SMC_NTC_SERIES_R};
static bool      sample_soon = false;  // The NTC settings changed: read on the next update()
static_assert(SMC_NTC_BETA >= min_ntc_beta && SMC_NTC_BETA <= max_ntc_beta, "SMC_NTC_BETA must be 1000-10000");
static_assert(SMC_NTC_R25 >= min_ntc_ohms && SMC_NTC_R25 <= max_ntc_ohms, "SMC_NTC_R25 must be 100-1000000");
static_assert(SMC_NTC_SERIES_R >= min_ntc_ohms && SMC_NTC_SERIES_R <= max_ntc_ohms, "SMC_NTC_SERIES_R must be 100-1000000");

const NtcConfig& ntcConfig() {
  return ntc;
}

bool setNtcConfig(const NtcConfig& config) {
  if (config.beta < min_ntc_beta || config.beta > max_ntc_beta || config.r25 < min_ntc_ohms || config.r25 > max_ntc_ohms || config.series_r < min_ntc_ohms ||
      config.series_r > max_ntc_ohms) {
    return false;
  }
  ntc = config;
  if (!ntc.enabled) {
    sensors.ntc_connected = false;
  }
  // Read with the new values on the next tick, not up to a second later: the
  // sensor bypass takes the CPU temperature from it from power on
  sample_soon = true;
  return true;
}

static uint8_t tick_counter = 0;
static int16_t logged_pico = INT16_MIN;  // Whole °C last logged
static int16_t logged_ntc = INT16_MIN;

// RP2040/RP2350 datasheet: 0.706 V at 27 °C, -1.721 mV per °C
static int16_t picoTemperature(const uint16_t raw) {
  const float volts = raw * adc_vref / adc_full_scale;
  return static_cast<int16_t>(lroundf((27.0f - (volts - 0.706f) / 0.001721f) * 10.0f));
}

// NTC from the pin to ground, ntc.series_r from 3.3 V to the pin. Beta
// equation from the resistance at 25 °C.
static int16_t ntcTemperature(const uint16_t raw) {
  const float resistance = static_cast<float>(ntc.series_r) * raw / (adc_full_scale - raw);
  const float kelvin = 1.0f / (1.0f / 298.15f + logf(resistance / static_cast<float>(ntc.r25)) / static_cast<float>(ntc.beta));
  return static_cast<int16_t>(lroundf((kelvin - 273.15f) * 10.0f));
}

static void logChange(const char* name, const int16_t tenths, int16_t& logged) {
  const int16_t whole = tenths / 10;
  if (whole == logged) {
    return;
  }

  logged = whole;
  const int magnitude = std::abs(tenths);
  debug::print(debug::Level::Debug, "TEMP: %s %s%d.%d C", name, tenths < 0 ? "-" : "", magnitude / 10, magnitude % 10);
}

static void sample() {
  sensors.pico_temperature = picoTemperature(hal::read_pico_temperature_adc());
  logChange("Pico", sensors.pico_temperature, logged_pico);

  if (!ntc.enabled) {
    return;
  }

  const uint16_t raw = hal::read_ntc_adc();
  const bool     connected = raw > ntc_limit && raw < adc_full_scale - ntc_limit;
  if (connected != sensors.ntc_connected) {
    debug::print(debug::Level::Info, connected ? "TEMP: NTC connected" : "TEMP: NTC open or shorted");
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
  if (++tick_counter < sample_ticks && !sample_soon) {
    return;
  }

  tick_counter = 0;
  sample_soon = false;
  sample();
}

}  // namespace ExtraTemp
}  // namespace SMC
