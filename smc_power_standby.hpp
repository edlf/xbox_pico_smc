#ifndef __SMC_POWER_STANDBY_SW__
#define __SMC_POWER_STANDBY_SW__

#include <cstdint>

namespace SMC {
namespace PowerStandby {

enum class power_standby_state {
  initial,
  read_av,
  check_av_power_button,
  turn_on_power_no_av,
  powered_up_wait_power_ok,
  check_eject_button,
  idle,
  turn_on_power_alternative,
  powered_up_wait_power_ok_alternative,
  powered_up_alt,
  powered_up
};

void init();
uint8_t update();
void setPowerTimeout3(uint16_t val);
uint16_t getPowerTimeout3();
void powerTimeout3Decrement();

} // namespace PowerStandby
} // namespace SMC

#endif // __SMC_POWER_STANDBY_SW__
