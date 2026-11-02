#ifndef SMC_EXTRA_TEMP_HPP
#define SMC_EXTRA_TEMP_HPP

#include <cstdint>

namespace SMC {
namespace ExtraTemp {

// Temperatures the PIC doesn't have: the Pico's own chip and an optional NTC
// thermistor (SMC_NTC_SENSOR). Read once a second, in standby and while the
// Xbox runs. Not reported over SMBus yet.
struct sensors_struct {
  int16_t pico_temperature;  // Tenths of °C
  int16_t ntc_temperature;   // Tenths of °C, valid when ntc_connected
  bool    ntc_connected;     // SMC_NTC_SENSOR and the reading isn't an open or short
};
extern sensors_struct sensors;

void init();
// Called once per tick
void update();

}  // namespace ExtraTemp
}  // namespace SMC

#endif  // SMC_EXTRA_TEMP_HPP
