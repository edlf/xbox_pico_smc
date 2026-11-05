#include "hardware/adc.h"

#include "hal.hpp"
#include "hal_internal.hpp"
#include "pin_assignments.hpp"

namespace hal {

// ADC: the RP2040/RP2350's internal temperature sensor and the optional NTC.
// Each read averages a few conversions (2 us each) to smooth the noise.
constexpr uint8_t ADC_SAMPLES = 16;

static uint16_t readAdc(const uint channel) {
  adc_select_input(channel);
  uint32_t sum = 0;
  for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
    sum += adc_read();
  }
  return static_cast<uint16_t>(sum / ADC_SAMPLES);
}

void init_adc() {
  adc_init();
  adc_set_temp_sensor_enabled(true);
  // Always: the NTC is a setting. GP28 isn't used for anything else.
  adc_gpio_init(pins::NTC_ADC);  // Digital input off, no pulls
}

uint16_t read_pico_temperature_adc() {
  return readAdc(ADC_TEMPERATURE_CHANNEL_NUM);
}

uint16_t read_ntc_adc() {
  return readAdc(pins::NTC_ADC - ADC_BASE_PIN);
}

}  // namespace hal
