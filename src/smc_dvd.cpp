#include "smc_dvd.hpp"
#include "smc.hpp"
#include "hal.hpp"

namespace SMC {
namespace Dvd {

flags_struct   flags;
sensors_struct sensors;

#if SMC_DVD_SUPPORT

drive_tray_state tray_status_first_read;

dvd_tray_state              dvd_tray;
update_dvd_tray_three_state dvd_tray_three;
update_eject_tray_state     update_eject_tray;

uint16_t tray_state_timer = 0;
uint16_t eject_timeout = 0;
uint16_t dvd_tray_timeout = 0;
// Where the tray was last sent or seen going (0x4D0 on the PIC: 0 open, 1 closed)
enum class tray_position : bool { open, closed };
tray_position    tray_position_state;
drive_tray_state tray_status_raw = drive_tray_state::closed;

void init() {
  update_eject_tray = update_eject_tray_state::initial;
  dvd_tray_three = update_dvd_tray_three_state::initial;
  dvd_tray = dvd_tray_state::initial;
  eject_timeout = 0;
  dvd_tray_timeout = 0;
  tray_position_state = tray_position::closed;  // Assume closed, same as the PIC
  tray_status_raw = drive_tray_state::closed;
}

void initDvdTray() {
  dvd_tray = dvd_tray_state::initial;
}

bool isTrayClosing() {
  return tray_position_state == tray_position::closed;
}

drive_tray_state readTrayState() {
  return static_cast<drive_tray_state>(hal::get_tray_state() & 0x70);
}

void updateDvdTray() {
  // Debounces the tray state and raises tray interrupts (0x9D3)
  switch (dvd_tray) {
    case dvd_tray_state::initial:
      tray_state_timer = 0x27;  // 39 ticks as P2L (0x109C), the PIC waits 32
      dvd_tray = dvd_tray_state::startup_delay;
      break;

    case dvd_tray_state::startup_delay:
      tray_state_timer--;
      if (tray_state_timer == 0) {
        dvd_tray = dvd_tray_state::watch;
      }
      break;

    case dvd_tray_state::watch:
      tray_status_raw = readTrayState();
      tray_status_first_read = tray_status_raw;
      if (sensors.tray_status != tray_status_raw) {
        dvd_tray = dvd_tray_state::confirm;
      }
      break;

    case dvd_tray_state::confirm:
      tray_status_raw = readTrayState();
      if (tray_status_raw != tray_status_first_read) {
        dvd_tray = dvd_tray_state::watch;
      } else {
        dvd_tray = dvd_tray_state::apply;
      }
      break;

    case dvd_tray_state::apply:
      debug::print_debug("DVD: Tray state " + std::to_string(static_cast<uint8_t>(tray_status_raw) >> 4));
      sensors.tray_status = tray_status_raw;
      if (tray_status_raw == drive_tray_state::opening) {
        if (flags.tray_events_enabled) {
          SMI::flags.tray_opening_event = true;
        }
      } else if (tray_status_raw == drive_tray_state::closed) {
        if (flags.tray_events_enabled) {
          SMI::flags.tray_state_00_event = true;
        }
      } else if (tray_status_raw == drive_tray_state::disc_detected || tray_status_raw == drive_tray_state::no_disc) {
        if (flags.tray_events_enabled) {
          SMI::flags.tray_media_event = true;
        }
      } else {
        flags.tray_events_enabled = true;
      }
      dvd_tray = dvd_tray_state::watch;
      break;

    default:
      dvd_tray = dvd_tray_state::initial;
      break;
  }
}

void restartTrayDebounce() {
  // Same as P11 (0x636): back to the start-up delay without reloading the timer,
  // which is already 0, so it counts 256 ticks
  dvd_tray = dvd_tray_state::startup_delay;
}

void restartTrayTracking() {
  // Boot challenge start restarts the debounce and tray moving (P2L, 0xD61)
  dvd_tray = dvd_tray_state::initial;
  dvd_tray_three = update_dvd_tray_three_state::initial;
}

void updateEjectTray() {
  // Pulses the drive's eject line for 5 ticks when eject_pulse is set (0xADE)
  switch (update_eject_tray) {
    case update_eject_tray_state::initial:
      eject_timeout = 5;
      if (flags.eject_pulse) {
        debug::print_message(std::string("DVD: Eject pulse to ") + (tray_position_state == tray_position::closed ? "close" : "open"));
        update_eject_tray = update_eject_tray_state::tick_timer;
      }
      break;

    case update_eject_tray_state::tick_timer:
      hal::dvd_eject_assert();
      if (--eject_timeout == 0) {
        update_eject_tray = update_eject_tray_state::release_eject;
      }
      break;

    case update_eject_tray_state::release_eject:
      flags.eject_pulse = false;
      hal::dvd_eject_release();
      update_eject_tray = update_eject_tray_state::initial;
      break;

    default:
      update_eject_tray = update_eject_tray_state::initial;
      break;
  }
}

// The PIC never pulses eject while the drive reports 0x70 or 0x20
static bool trayCanMove() {
  const drive_tray_state tray_state = readTrayState();
  return tray_state != drive_tray_state::drive_off && tray_state != drive_tray_state::unloading;
}

// The tray state that ends an open or closed phase: closing (0x50) or opening
// (0x30). As P2L (0x1112) it also follows a tray that reports closed (0x00) or
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
  // Decides when to pulse eject (0x4D0). While the tray is open the SMC closes
  // it on its own; while closed, the eject button only raises an SMI (via
  // eject_change_requested) and the OS opens the tray with SMBus 0x0C.
  switch (tray_position_state) {
    case tray_position::open:
      if (nowClosed()) {
        /* Tray is closing */
        flags.eject_change_requested = false;
        flags.tray_load_request = false;
        tray_position_state = tray_position::closed;
        return;
      }

      /* Close on eject button, load request (SMBus 0x0C) or shutdown */
      if (!flags.eject_change_requested && !flags.tray_load_request && !SMI::flags.prepare_for_shutdown) {
        return;
      }

      if (!trayCanMove()) {
        return;
      }
      flags.eject_pulse = true;         // Pulse eject
      flags.eject_button_event = true;  // Track the move, as P2L (0x113D)
      flags.eject_change_requested = false;
      flags.tray_load_request = false;
      tray_position_state = tray_position::closed;
      break;

    case tray_position::closed:
      if (SMI::flags.prepare_for_shutdown) {
        return;
      }

      if (nowOpen()) {
        /* Tray is opening */
        flags.eject_change_requested = false;
        flags.tray_eject_request = false;
        tray_position_state = tray_position::open;
        return;
      }

      if (flags.eject_change_requested) {
        if (!SMI::flags.reset_on_eject_disabled) {
          flags.tray_open_expected = true;
        }

        /* FRAG'd: no OS to ask, open the tray directly */
        if (BootChallenge::flags.frag_eject) {
          PLL_Reset::flags.run_after_reset = true;
          flags.eject_pulse = true;
          flags.eject_change_requested = false;
        }
      }

      /* Eject request from the OS (SMBus 0x0C) */
      if (!flags.tray_eject_request || !trayCanMove()) {
        return;
      }

      flags.eject_pulse = true;         // Pulse eject
      flags.eject_button_event = true;  // Track the move, as P2L (0x113D)
      flags.eject_change_requested = false;
      flags.tray_eject_request = false;
      tray_position_state = tray_position::open;
      break;
  }
}

void updateDvdTrayThree() {
  // Tracks whether the tray is moving, reported in SMBus 0x03 bit 0 (0x519)
  switch (dvd_tray_three) {
    case update_dvd_tray_three_state::initial:
      dvd_tray_timeout = 0xFF;
      flags.tray_moving = false;

      if (flags.eject_button_event) {
        dvd_tray_three = update_dvd_tray_three_state::eject;
      } else {
        if (!(sensors.tray_status == drive_tray_state::drive_off || sensors.tray_status == drive_tray_state::open ||
              sensors.tray_status == drive_tray_state::closed || sensors.tray_status == drive_tray_state::no_disc ||
              sensors.tray_status == drive_tray_state::disc_detected)) {
          dvd_tray_three = update_dvd_tray_three_state::wait;
        }
      }
      break;

    case update_dvd_tray_three_state::wait:
      flags.tray_moving = true;
      flags.eject_button_event = false;

      if (sensors.tray_status == drive_tray_state::open || sensors.tray_status == drive_tray_state::no_disc ||
          sensors.tray_status == drive_tray_state::disc_detected) {
        dvd_tray_three = update_dvd_tray_three_state::initial;
      }
      break;

    case update_dvd_tray_three_state::eject:
      flags.tray_moving = true;

      // Closing (0x50) always ends it, as P2L (0x6E1); the PIC ignores it while
      // the tray is expected to open
      if (sensors.tray_status == drive_tray_state::closing || sensors.tray_status == drive_tray_state::opening ||
          sensors.tray_status == drive_tray_state::unloading || (SMI::flags.os_ready && --dvd_tray_timeout == 0)) {
        flags.tray_open_expected = false;
        dvd_tray_three = update_dvd_tray_three_state::wait;
      }
      break;

    default:
      dvd_tray_three = update_dvd_tray_three_state::initial;
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
