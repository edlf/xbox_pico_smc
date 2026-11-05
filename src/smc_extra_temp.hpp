#ifndef SMC_EXTRA_TEMP_HPP
#define SMC_EXTRA_TEMP_HPP

#include <cstdint>

namespace SMC {
namespace ExtraTemp {

// Temperatures the PIC doesn't have: the Pico's own chip and an optional NTC
// thermistor (ntcConfig()). Read once a second, in standby and while the
// Xbox runs. Reported on SMBus 0x31 and 0x32.
struct sensors_struct {
  int16_t pico_temperature;  // Tenths of °C
  int16_t ntc_temperature;   // Tenths of °C, valid when ntc_connected
  bool    ntc_connected;     // NTC on and the reading isn't an open or short
};
extern sensors_struct sensors;

// The NTC: on GP28 to ground, series_r from 3.3 V to the pin, beta equation
// with beta and the resistance at 25 °C (r25). Saved setting, SMC_NTC_* by
// default.
struct NtcConfig {
  bool     enabled;
  uint16_t beta;      // K
  uint32_t r25;       // Ohms
  uint32_t series_r;  // Ohms
};
constexpr uint16_t min_ntc_beta = 1000, max_ntc_beta = 10000;
constexpr uint32_t min_ntc_ohms = 100, max_ntc_ohms = 1000000;
const NtcConfig&   ntcConfig();
// False (and nothing changed) when a value is out of range
bool setNtcConfig(const NtcConfig& config);

// Shut the Xbox down, as an overheat, once the NTC reads limit_c or more: for
// an NTC on something the ADM1032 doesn't see. Saved setting, SMC_NTC_SHUTDOWN
// (off) and SMC_NTC_SHUTDOWN_TEMP by default. Ignored while the sensor bypass
// is on (the NTC is the CPU temperature then, with its own thresholds), and
// while the NTC is off or not connected.
struct NtcShutdown {
  bool    enabled;
  uint8_t limit_c;  // °C
};
constexpr uint8_t  min_ntc_shutdown = 40, max_ntc_shutdown = 120;
const NtcShutdown& ntcShutdown();
bool               setNtcShutdown(const NtcShutdown& shutdown);  // False (and unchanged) outside 40-120 °C
// The check applies and the NTC is at or over its limit
bool ntcOverheated();

void init();
// Called once per tick
void update();

}  // namespace ExtraTemp
}  // namespace SMC

#endif  // SMC_EXTRA_TEMP_HPP
