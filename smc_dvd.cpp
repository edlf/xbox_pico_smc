#include "smc_dvd.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"

namespace SMC {
namespace Dvd {

#if SMC_DVD_SUPPORT

uint8_t raw_tray_status_filtered;

dvd_tray_state dvd_tray;
update_dvd_tray_three_state dvd_tray_three; // jump_index_sub_code_519
update_eject_tray_state update_eject_tray;

uint16_t tray_state_timer = 0;
uint16_t eject_timeout = 0;
uint16_t dvd_tray_timeout = 0;
uint8_t tray_eject; // 0 = tray open, 1 = tray closed
uint8_t tray_status_raw = 0;

void init() {
  update_eject_tray = update_eject_tray_state::initial;
  dvd_tray_three = update_dvd_tray_three_state::initial;
  dvd_tray = dvd_tray_state::initial;
  eject_timeout = 0;
  dvd_tray_timeout = 0;
  tray_eject = 1; // Assume closed, same as the PIC
  tray_status_raw = 0;
}

void initDdvdTray() {
  dvd_tray = dvd_tray_state::initial;
}

bool isTrayClosing() {
  return (tray_eject == 1);
}

uint8_t readTrayState() {
  return hal::get_tray_state() & 0x70;
}

void updateDvdTray() {
  // Debounces the tray state and raises tray interrupts (0x9D3)
  uint8_t tray_state;

  switch (dvd_tray) {
  case dvd_tray_state::initial:
    tray_state_timer = 0x20;
    dvd_tray = dvd_tray_state::tick_timer;
    break;

  case dvd_tray_state::tick_timer:
    tray_state_timer--;
    if (tray_state_timer == 0) {
      dvd_tray = dvd_tray_state::state2;
    }
    break;

  case dvd_tray_state::state2:
    tray_status_raw = hal::get_tray_state() & 0x70;
    raw_tray_status_filtered = tray_status_raw;
    if ((sensors.tray_status ^ tray_status_raw) != 0) {
      dvd_tray = dvd_tray_state::state3;
    }
    break;

  case dvd_tray_state::state3:
    tray_state = hal::get_tray_state();
    tray_status_raw = tray_state & 0x70;
    if ((tray_state & 0x70) != raw_tray_status_filtered) {
      dvd_tray = dvd_tray_state::state2;
    } else {
      dvd_tray = dvd_tray_state::state4;
    }
    break;

  case dvd_tray_state::state4:
    debug::print_message("DVD: Tray state " + std::to_string(tray_status_raw >> 4));
    sensors.tray_status = tray_status_raw;
    if (tray_status_raw == 0x30) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        utils::setBitNo(flags.bitfield_DATA_71, 2);
      }
    } else if (tray_status_raw == 0) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        utils::setBitNo(flags.bitfield_DATA_73, 5);
      }
    } else if ((tray_status_raw == 0x60) || (tray_status_raw == 0x40)) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        setStatusBit(Status::dvd_tray);
      }
    } else {
      utils::setBitNo(flags.bitfield_DATA_72, 4);
    }
    dvd_tray = dvd_tray_state::state2;
    break;

  default:
    dvd_tray = dvd_tray_state::initial;
    break;
  }
}

void updateEjectTray() {
  // Pulses the drive's eject line for 5 ticks when 72.3 is set (0xADE)
  switch (update_eject_tray) {
  case update_eject_tray_state::initial:
    eject_timeout = 5;
    if (utils::checkBitNo(flags.bitfield_DATA_72, 3) != 0) {
      debug::print_message(std::string("DVD: Eject pulse to ") + (tray_eject ? "close" : "open"));
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
    utils::clearBitNo(flags.bitfield_DATA_72, 3);
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
  const uint8_t tray_state = hal::get_tray_state() & 0x70;
  return (tray_state != 0x70) && (tray_state != 0x20);
}

void updateDvdTrayEject() {
  // Decides when to pulse eject (0x4D0). While the tray is open the SMC closes
  // it on its own; while closed, the eject button only raises an SMI (via
  // 6E.1) and the OS opens the tray with SMBus 0x0C.
  switch (tray_eject) {
  case 0: /* Tray open */
    if (sensors.tray_status == 0x50) {
      /* Tray is closing */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x10;
      tray_eject = 1;
      return;
    }

    /* Close on eject button, load request (SMBus 0x0C) or shutdown */
    if (!checkStatusBit(eject_change_requested) && !(flags.bitfield_DATA_6F & 0x10) &&
        !checkStatusBit(prepare_for_shutdown)) {
      return;
    }
    if (!trayCanMove()) {
      return;
    }
    flags.bitfield_DATA_72 |= 0x08; // Pulse eject
    clearStatusBit(eject_change_requested);
    flags.bitfield_DATA_6F &= ~0x10;
    tray_eject = 1;
    break;

  case 1: /* Tray closed */
    if (checkStatusBit(prepare_for_shutdown)) {
      return;
    }

    if (sensors.tray_status == 0x30) {
      /* Tray is opening */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x08;
      tray_eject = 0;
      return;
    }

    if (checkStatusBit(eject_change_requested)) {
      if ((flags.bitfield_DATA_72 & 0x02) == 0) {
        flags.bitfield_DATA_73 |= 0x02;
      }

      /* FRAG'd: no OS to ask, open the tray directly */
      if (flags.bitfield_DATA_73 & 0x04) {
        flags.bitfield_DATA_70 |= 0x02;
        flags.bitfield_DATA_72 |= 0x08;
        clearStatusBit(eject_change_requested);
      }
    }

    /* Eject request from the OS (SMBus 0x0C) */
    if (!(flags.bitfield_DATA_6F & 0x08) || !trayCanMove()) {
      return;
    }
    flags.bitfield_DATA_72 |= 0x08; // Pulse eject
    clearStatusBit(eject_change_requested);
    flags.bitfield_DATA_6F &= ~0x08;
    tray_eject = 0;
    break;

  default:
    tray_eject = 0;
    break;
  }
}

void updateDvdTrayThree() {
  // Tracks whether the tray is moving, reported in SMBus 0x03 bit 0 (0x519)
  switch (dvd_tray_three) {
  case update_dvd_tray_three_state::initial:
    dvd_tray_timeout = 0xFF;
    utils::clearBitNo(flags.bitfield_DATA_6F, 5);

    if (utils::checkBitNo(flags.bitfield_DATA_72, 7)) {
      dvd_tray_three = update_dvd_tray_three_state::eject;
    } else {
      if (!(sensors.tray_status == 0x70 ||
            sensors.tray_status == 0x10 ||
            sensors.tray_status == 0x00 ||
            sensors.tray_status == 0x40 ||
            sensors.tray_status == 0x60)) {
        dvd_tray_three = update_dvd_tray_three_state::wait;
      }
    }
    break;

  case update_dvd_tray_three_state::wait:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);
    utils::clearBitNo(flags.bitfield_DATA_72, 7);

    if (sensors.tray_status == 0x10 ||
        sensors.tray_status == 0x40 ||
        sensors.tray_status == 0x60) {
      dvd_tray_three = update_dvd_tray_three_state::initial;
    }
    break;

  case update_dvd_tray_three_state::eject:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);

    if ((!utils::checkBitNo(flags.bitfield_DATA_73, 1) && sensors.tray_status == 0x50) ||
        sensors.tray_status == 0x30 ||
        sensors.tray_status == 0x20 ||
        (utils::checkBitNo(flags.bitfield_DATA_72, 6) && --dvd_tray_timeout == 0)) {
      utils::clearBitNo(flags.bitfield_DATA_73, 1);
      dvd_tray_three = update_dvd_tray_three_state::wait;
    }
    break;

  default:
    dvd_tray_three = update_dvd_tray_three_state::initial;
    break;
  }
}

#else // !SMC_DVD_SUPPORT

// No drive: the tray reads as closed and empty, so the OS doesn't wait for
// media and shutdown doesn't wait for the tray. The eject button still
// raises an SMI (handled by the SMI state machine).
constexpr uint8_t no_drive_tray_state = 0x40;

void init() {}

void initDdvdTray() {}

bool isTrayClosing() {
  return true;
}

uint8_t readTrayState() {
  return no_drive_tray_state;
}

void updateDvdTray() {
  sensors.tray_status = no_drive_tray_state;
}

void updateEjectTray() {}

void updateDvdTrayEject() {}

void updateDvdTrayThree() {}

#endif // SMC_DVD_SUPPORT

} // namespace DVD
} // namespace SMC
