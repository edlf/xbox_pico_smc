#ifndef __FSM_LED__
#define __FSM_LED__

#include "smc_types.hpp"

namespace SMC {

enum class led_state {
  initial,
  overheat,
  manual_control,
  quick_green_blink,
  solid_green,
  off,
  tick_update,
  set_gpios,
  reset_phase_counter,
  quick_green_orange
};

class FSM_Leds {

public:
  FSM_Leds();
  ~FSM_Leds();
  void update(
    const bool overheat,
    const bool av_missing,
    const bool power_off_bit,
    const bool quick_green_blink);
  void reset_phase_counter();
  void reset_state();
  void setGreenPhases(const uint8_t);
  void setRedPhases(const uint8_t);
  void setManualControl(const bool mc);

  void printState();

private:
  led_state state;
  uint8_t state_counter;
  uint8_t green_phases_manual;
  uint8_t red_phases_manual;
  uint8_t green_phases;
  uint8_t red_phases;
  bool manual_control;
};

}; // namespace SMC

#endif // __FSM_LED__
