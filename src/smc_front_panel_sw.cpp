#include "smc.hpp"
#include "hal.hpp"

#include <iterator>

namespace SMC {
namespace FrontPanelSW {

// Same as the PIC: a press or release only counts
// once it's read on two ticks in a row
struct Debouncer {
  bool pressed = false;   // Debounced level
  bool last_raw = false;  // Previous reading

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

static bool power_button = true;
static bool eject_button = true;
static bool bootsel_power = SMC_BOOTSEL_POWER;

bool powerButtonEnabled() {
  return power_button;
}

void setPowerButtonEnabled(const bool on) {
  power_button = on;
}

bool ejectButtonEnabled() {
  return eject_button;
}

void setEjectButtonEnabled(const bool on) {
  eject_button = on;
}

bool bootselPowerSetting() {
  return bootsel_power;
}

bool bootselIsPower() {
  return bootsel_power || !power_button;
}

void setBootselPower(const bool on) {
  bootsel_power = on;
}

// The power button and/or BOOTSEL, as set
static bool powerPressed() {
  const bool button = power_button && hal::power_button_pressed();
  return button || (bootselIsPower() && hal::bootsel_button_pressed());
}

bool pressPower() {
  if (!eject.idle()) {
    return false;
  }
  SMI::flags.power_change_requested = true;
  return true;
}

bool pressEject() {
  if (!power.idle()) {
    return false;
  }
  Dvd::flags.eject_change_requested = true;
  Dvd::flags.eject_button_event = true;
  return true;
}

void init() {
  power = {};
  eject = {};
}

void update() {
  updatePower();
  updateEject();
}

void updatePower() {
  // Only counts while the eject button is idle (as P2L)
  if (power.update(powerPressed())) {
    pressPower();
  }
}

void updateEject() {
  // Only counts while the power button is idle (as P2L)
  if (eject.update(eject_button && hal::eject_button_pressed())) {
    pressEject();
  }
}

void printPowerState() {
  debug::print(debug::Level::Info, "Power SW state: %s", power.stateName());
}

void printEjectState() {
  debug::print(debug::Level::Info, "Eject SW state: %s", eject.stateName());
}

}  // namespace FrontPanelSW
}  // namespace SMC
