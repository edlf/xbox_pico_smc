#include "smc.hpp"
#include "hal.hpp"

#include <iterator>
#include <string>

namespace SMC {
namespace FrontPanelSW {

// Same as the PIC (0x800 power, 0x828 eject): a press or release only counts
// once it's read on two ticks in a row
struct Debouncer {
  bool pressed = false;  // Debounced level
  bool last_raw = false; // Previous reading

  // Called once per tick, true on the tick a press is confirmed
  bool update(const bool raw) {
    const bool stable = (raw == last_raw);
    last_raw = raw;
    if (stable && raw != pressed) {
      pressed = raw;
      return pressed;
    }
    return false;
  }

  // Released and no press being confirmed
  bool idle() const {
    return !pressed && !last_raw;
  }

  // The PIC's four states, for the log
  const char* stateName() const {
    if (!pressed) {
      return last_raw ? "Debouncing" : "Wait for button press";
    }
    return last_raw ? "Wait for button release" : "Release debounce";
  }
};

static Debouncer power;
static Debouncer eject;

void init() {
  power = {};
  eject = {};
}

void update() {
  updatePower();
  updateEject();
}

void updatePower() {
  // Only counts while the eject button is idle (as P2L, 0x8C3)
  if (power.update(hal::power_button_pressed()) && eject.idle()) {
    SMI::flags.power_change_requested = true;
  }
}

void updateEject() {
  // Only counts while the power button is idle (as P2L, 0x782)
  if (eject.update(hal::eject_button_pressed()) && power.idle()) {
    Dvd::flags.eject_change_requested = true;
    Dvd::flags.eject_button_event = true;
  }
}

void printPowerState() {
  debug::print_message(std::string("Power SW state: ") + power.stateName());
}

void printEjectState() {
  debug::print_message(std::string("Eject SW state: ") + eject.stateName());
}

} // namespace FrontPanelSW
} // namespace SMC
