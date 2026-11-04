#include "smc_dvd.hpp"
#include "smc.hpp"
#include "hal.hpp"

namespace SMC {
namespace Dvd {

flags_struct   flags;
sensors_struct sensors;

#if SMC_DVD_SUPPORT

// updateDvdTray(): debounces the drive's tray state
struct Debounce {
  dvd_tray_state   state;
  uint16_t         timer = 0;  // Start-up delay
  drive_tray_state first_read;
  drive_tray_state raw = drive_tray_state::closed;
};
static Debounce debounce;

// updateEjectTray(): the eject line pulse
struct EjectPulse {
  update_eject_tray_state state;
  uint16_t                timeout = 0;
};
static EjectPulse eject;

// updateDvdTrayThree(): whether the tray is moving
struct TrayMoving {
  update_dvd_tray_three_state state;
  uint16_t                    timeout = 0;
};
static TrayMoving moving;

// Where the tray was last sent or seen going (0 open, 1 closed)
enum class tray_position : bool { open, closed };
static tray_position tray_position_state;

void init() {
  eject = {};
  moving = {};
  debounce.state = dvd_tray_state::initial;
  debounce.raw = drive_tray_state::closed;
  tray_position_state = tray_position::closed;  // Assume closed, same as the PIC
}

void initDvdTray() {
  debounce.state = dvd_tray_state::initial;
}

bool isTrayClosing() {
  return tray_position_state == tray_position::closed;
}

drive_tray_state readTrayState() {
  return static_cast<drive_tray_state>(hal::get_tray_state() & 0x70);
}

void updateDvdTray() {
  // Debounces the tray state and raises tray interrupts
  switch (debounce.state) {
    case dvd_tray_state::initial:
      debounce.timer = 0x27;  // 39 ticks as P2L, the PIC waits 32
      debounce.state = dvd_tray_state::startup_delay;
      break;

    case dvd_tray_state::startup_delay:
      debounce.timer--;
      if (debounce.timer == 0) {
        debounce.state = dvd_tray_state::watch;
      }
      break;

    case dvd_tray_state::watch:
      debounce.raw = readTrayState();
      debounce.first_read = debounce.raw;
      if (sensors.tray_status != debounce.raw) {
        debounce.state = dvd_tray_state::confirm;
      }
      break;

    case dvd_tray_state::confirm:
      debounce.raw = readTrayState();
      if (debounce.raw != debounce.first_read) {
        debounce.state = dvd_tray_state::watch;
      } else {
        debounce.state = dvd_tray_state::apply;
      }
      break;

    case dvd_tray_state::apply:
      debug::print_debug("DVD: Tray state " + std::to_string(static_cast<uint8_t>(debounce.raw) >> 4));
      sensors.tray_status = debounce.raw;
      if (debounce.raw == drive_tray_state::opening) {
        if (flags.tray_events_enabled) {
          SMI::flags.tray_opening_event = true;
        }
      } else if (debounce.raw == drive_tray_state::closed) {
        if (flags.tray_events_enabled) {
          SMI::flags.tray_state_00_event = true;
        }
      } else if (debounce.raw == drive_tray_state::disc_detected || debounce.raw == drive_tray_state::no_disc) {
        if (flags.tray_events_enabled) {
          SMI::flags.tray_media_event = true;
        }
      } else {
        flags.tray_events_enabled = true;
      }
      debounce.state = dvd_tray_state::watch;
      break;

    default:
      debounce.state = dvd_tray_state::initial;
      break;
  }
}

void restartTrayDebounce() {
  // Same as P11: back to the start-up delay without reloading the timer,
  // which is already 0, so it counts 256 ticks
  debounce.state = dvd_tray_state::startup_delay;
}

void restartTrayTracking() {
  // Boot challenge start restarts the debounce and tray moving (P2L)
  debounce.state = dvd_tray_state::initial;
  moving.state = update_dvd_tray_three_state::initial;
}

void updateEjectTray() {
  // Pulses the drive's eject line for 5 ticks when eject_pulse is set
  switch (eject.state) {
    case update_eject_tray_state::initial:
      eject.timeout = 5;
      if (flags.eject_pulse) {
        debug::print_message(std::string("DVD: Eject pulse to ") + (tray_position_state == tray_position::closed ? "close" : "open"));
        eject.state = update_eject_tray_state::tick_timer;
      }
      break;

    case update_eject_tray_state::tick_timer:
      hal::dvd_eject_assert();
      if (--eject.timeout == 0) {
        eject.state = update_eject_tray_state::release_eject;
      }
      break;

    case update_eject_tray_state::release_eject:
      flags.eject_pulse = false;
      hal::dvd_eject_release();
      eject.state = update_eject_tray_state::initial;
      break;

    default:
      eject.state = update_eject_tray_state::initial;
      break;
  }
}

// The PIC never pulses eject while the drive reports 0x70 or 0x20
static bool trayCanMove() {
  const drive_tray_state tray_state = readTrayState();
  return tray_state != drive_tray_state::drive_off && tray_state != drive_tray_state::unloading;
}

// The tray state that ends an open or closed phase: closing (0x50) or opening
// (0x30). As P2L it also follows a tray that reports closed (0x00) or
// open (0x10) while it isn't moving, except right after an eject press or pulse
// (the PIC only follows 0x50 and 0x30).
static bool nowClosed() {
  return !flags.eject_button_event &&
         (sensors.tray_status == drive_tray_state::closing || (!flags.tray_moving && sensors.tray_status == drive_tray_state::closed));
}

static bool nowOpen() {
  return !flags.eject_button_event &&
         (sensors.tray_status == drive_tray_state::opening || (!flags.tray_moving && sensors.tray_status == drive_tray_state::open));
}

void updateDvdTrayEject() {
  // Decides when to pulse eject. While the tray is open the SMC closes
  // it on its own; while closed, the eject button only raises an SMI (via
  // eject_change_requested) and the OS opens the tray with SMBus 0x0C.
  switch (tray_position_state) {
    case tray_position::open:
      if (nowClosed()) {
        // Tray is closing
        flags.eject_change_requested = false;
        flags.tray_load_request = false;
        tray_position_state = tray_position::closed;
        return;
      }

      // Close on eject button, load request (SMBus 0x0C) or shutdown
      if (!flags.eject_change_requested && !flags.tray_load_request && !SMI::flags.prepare_for_shutdown) {
        return;
      }

      if (!trayCanMove()) {
        return;
      }
      flags.eject_pulse = true;
      flags.eject_button_event = true;  // Track the move, as P2L
      flags.eject_change_requested = false;
      flags.tray_load_request = false;
      tray_position_state = tray_position::closed;
      break;

    case tray_position::closed:
      if (SMI::flags.prepare_for_shutdown) {
        return;
      }

      if (nowOpen()) {
        // Tray is opening
        flags.eject_change_requested = false;
        flags.tray_eject_request = false;
        tray_position_state = tray_position::open;
        return;
      }

      if (flags.eject_change_requested) {
        if (!SMI::flags.reset_on_eject_disabled) {
          flags.tray_open_expected = true;
        }

        // FRAG'd: no OS to ask, open the tray directly
        if (BootChallenge::flags.frag_eject) {
          PLL_Reset::flags.run_after_reset = true;
          flags.eject_pulse = true;
          flags.eject_change_requested = false;
        }
      }

      // Eject request from the OS (SMBus 0x0C)
      if (!flags.tray_eject_request || !trayCanMove()) {
        return;
      }

      flags.eject_pulse = true;
      flags.eject_button_event = true;  // Track the move, as P2L
      flags.eject_change_requested = false;
      flags.tray_eject_request = false;
      tray_position_state = tray_position::open;
      break;
  }
}

void updateDvdTrayThree() {
  // Tracks whether the tray is moving, reported in SMBus 0x03 bit 0
  switch (moving.state) {
    case update_dvd_tray_three_state::initial:
      moving.timeout = 0xFF;
      flags.tray_moving = false;

      if (flags.eject_button_event) {
        moving.state = update_dvd_tray_three_state::eject;
      } else {
        if (!(sensors.tray_status == drive_tray_state::drive_off || sensors.tray_status == drive_tray_state::open ||
              sensors.tray_status == drive_tray_state::closed || sensors.tray_status == drive_tray_state::no_disc ||
              sensors.tray_status == drive_tray_state::disc_detected)) {
          moving.state = update_dvd_tray_three_state::wait;
        }
      }
      break;

    case update_dvd_tray_three_state::wait:
      flags.tray_moving = true;
      flags.eject_button_event = false;

      if (sensors.tray_status == drive_tray_state::open || sensors.tray_status == drive_tray_state::no_disc ||
          sensors.tray_status == drive_tray_state::disc_detected) {
        moving.state = update_dvd_tray_three_state::initial;
      }
      break;

    case update_dvd_tray_three_state::eject:
      flags.tray_moving = true;

      // Closing (0x50) always ends it, as P2L; the PIC ignores it while
      // the tray is expected to open
      if (sensors.tray_status == drive_tray_state::closing || sensors.tray_status == drive_tray_state::opening ||
          sensors.tray_status == drive_tray_state::unloading || (SMI::flags.os_ready && --moving.timeout == 0)) {
        flags.tray_open_expected = false;
        moving.state = update_dvd_tray_three_state::wait;
      }
      break;

    default:
      moving.state = update_dvd_tray_three_state::initial;
      break;
  }
}

#else  // !SMC_DVD_SUPPORT

// No drive: the tray reads as closed and empty, so the OS doesn't wait for
// media and shutdown doesn't wait for the tray. The eject button still
// raises an SMI (handled by the SMI state machine).
constexpr drive_tray_state no_drive_tray_state = drive_tray_state::no_disc;

void init() {}

void initDvdTray() {}

bool isTrayClosing() {
  return true;
}

drive_tray_state readTrayState() {
  return no_drive_tray_state;
}

void updateDvdTray() {
  sensors.tray_status = no_drive_tray_state;
}

void restartTrayDebounce() {}
void restartTrayTracking() {}

void updateEjectTray() {}

void updateDvdTrayEject() {}

void updateDvdTrayThree() {}

#endif  // SMC_DVD_SUPPORT

}  // namespace Dvd
}  // namespace SMC
