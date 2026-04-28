#include "smc_dvd.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"

namespace SMC {
namespace Dvd {

uint8_t raw_tray_status_filtered;

dvd_tray_state dvd_tray;
uint8_t tray_eject;
update_dvd_tray_three_state dvd_tray_three; // jump_index_sub_code_519
update_eject_tray_state update_eject_tray;

uint16_t tray_state_timer;
uint16_t eject_timeout;
uint16_t dvd_tray_timeout;

void init() {
  update_eject_tray = update_eject_tray_state::initial;
  dvd_tray_three = update_dvd_tray_three_state::initial;
  dvd_tray = dvd_tray_state::initial;
  tray_eject = 0;
}

void initDdvdTray() {
  dvd_tray = dvd_tray_state::initial;
}

bool isTrayClosing() {
  return (tray_eject == 1);
}

void updateDvdTray() {
  // Done, untested
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
    sensors.tray_status_raw = hal::get_tray_state() & 0x70;
    raw_tray_status_filtered = sensors.tray_status_raw;
    if ((sensors.tray_status ^ sensors.tray_status_raw) != 0) {
      dvd_tray = dvd_tray_state::state3;
    }
    break;

  case dvd_tray_state::state3:
    tray_state = hal::get_tray_state();
    sensors.tray_status_raw = tray_state & 0x70;
    if ((tray_state & 0x70) != raw_tray_status_filtered) {
      dvd_tray = dvd_tray_state::state2;
    } else {
      dvd_tray = dvd_tray_state::state4;
    }
    break;

  case dvd_tray_state::state4:
    sensors.tray_status = sensors.tray_status_raw;
    if (sensors.tray_status_raw == 0x30) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        utils::setBitNo(flags.bitfield_DATA_71, 2);
      }
    } else if (sensors.tray_status_raw == 0) {
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        utils::setBitNo(flags.bitfield_DATA_73, 5);
      }
    } else {
      if ((sensors.tray_status_raw != 0x60) && (sensors.tray_status_raw != 0x40)) {
        dvd_tray = dvd_tray_state::state2;
        utils::setBitNo(flags.bitfield_DATA_72, 4);
        return;
      }
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        setStatusBit(InterruptReason_eject_sw_pressed);
      }
    }
    dvd_tray = dvd_tray_state::state2;
    return;
    break;

  default:
    dvd_tray = dvd_tray_state::initial;
    break;
  }
}

void updateEjectTray() {
  // Done, not tested

  switch (update_eject_tray) {
  case update_eject_tray_state::initial:
    eject_timeout = 5;

    // Check if eject signal is set
    if (utils::checkBitNo(flags.bitfield_DATA_72, 5) != 0) {
      update_eject_tray = update_eject_tray_state::tick_timer;
    }
    break;

  case update_eject_tray_state::tick_timer:
    hal::dvd_eject_off();
    if (--eject_timeout == 0) {
      update_eject_tray = update_eject_tray_state::release_eject;
    }
    break;

  case update_eject_tray_state::release_eject:
    utils::clearBitNo(flags.bitfield_DATA_72, 5);
    hal::dvd_eject_on();
    update_eject_tray = update_eject_tray_state::initial;
    break;

  default:
    update_eject_tray = update_eject_tray_state::initial;
    break;
  }
}

void updateDvdTrayEject() {
  /* Handle tray eject mechanism - manages DVD tray eject/inject operations
   * States: 0=check eject conditions, 1=wait for shutdown signal */

  switch (tray_eject) {
  case 0:
    /* Check if we should initiate tray eject */

    /* If tray is already at state 0x50 (fully ejected), nothing to do */
    if (sensors.tray_status == 0x50) {
      /* Tray already ejected - clear flags and return to state 1 */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x10; // Clear eject request
      tray_eject = 1;
      return;
    }

    /* Check if eject should be triggered */
    if ((checkStatusBit(eject_change_requested)) || // Eject button pressed
        (flags.bitfield_DATA_6F & 0x10)) {          // Eject request from elsewhere

      /* Check current tray state via PORTB bits 4-6 */
      uint8_t tray_state = hal::get_tray_state();

      /* Only proceed if tray is not already in eject state (0x70) or insert
       * state (0x20) */
      if (tray_state != 0x70 && tray_state != 0x20) {
        /* Signal tray eject mechanism */
        flags.bitfield_DATA_72 |= 0x08; // Set tray eject signal
      }
    }

    /* Clear flags and transition to state 1 */
    clearStatusBit(eject_change_requested); // Clear eject button
    flags.bitfield_DATA_6F &= ~0x10;        // Clear eject request
    tray_eject = 1;
    break;

  case 1:
    /* Wait state - only act if system is shutting down */
    if (!checkStatusBit(prepare_for_shutdown)) {
      /* System is still powered - stay in this state */
      return;
    }

    /* System is shutting down */

    /* If tray is at state 0x30 (fully inserted), transition back to state 0
     */
    if (sensors.tray_status == 0x30) {
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x10; // Clear eject request
      tray_eject = 0;
      return;
    }

    /* Check eject button and internal flags */
    if (checkStatusBit(eject_change_requested)) {
      /* Eject button is being pressed */
      if ((flags.bitfield_DATA_72 & 0x02) == 0) {
        /* Set initialization flag */
        flags.bitfield_DATA_73 |= 0x02;
      }

      /* Check for FRAG condition */
      if ((flags.bitfield_DATA_73 & 0x04) == 0) {
        /* Not FRAG'd - can continue */
      } else {
        /* System is FRAG'd - set error flags */
        flags.bitfield_DATA_70 |= 0x02;
        flags.bitfield_DATA_72 |= 0x08; // Set eject signal
        clearStatusBit(eject_change_requested);
      }
    }

    /* Check if explicit eject request is set */
    if (flags.bitfield_DATA_6F & 0x10) {
      uint8_t tray_state = hal::get_tray_state();

      /* Only proceed if not already in eject/insert states */
      if (tray_state != 0x70 && tray_state != 0x20) {
        flags.bitfield_DATA_72 |= 0x08; // Signal eject
      }
    }
    break;

  default:
    tray_eject = 0;
    break;
  }
}

void updateDvdTrayThree() {
  // Done, untested
  switch (dvd_tray_three) {
  case update_dvd_tray_three_state::initial:
    dvd_tray_timeout = 0xFF;
    utils::clearBitNo(flags.bitfield_DATA_6F, 5);

    if (utils::checkBitNo(flags.bitfield_DATA_72, 7)) {
      dvd_tray_three = update_dvd_tray_three_state::eject;
    } else {
      if (!(sensors.tray_status == 0x70 ||  // Eject state
            sensors.tray_status == 0x10 ||  // State 1
            sensors.tray_status == 0x00 ||  // State 0
            sensors.tray_status == 0x40 ||  // State 4
            sensors.tray_status == 0x60)) { // State 6
        dvd_tray_three = update_dvd_tray_three_state::wait;
      }
    }
    break;

  case update_dvd_tray_three_state::wait:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);
    utils::clearBitNo(flags.bitfield_DATA_72, 7);

    // Check if tray reached a stable position
    if (sensors.tray_status == 0x10 || // Stable position 1
        sensors.tray_status == 0x40 || // Stable position 4
        sensors.tray_status == 0x60) { // Stable position 6
      dvd_tray_three = update_dvd_tray_three_state::initial;
    }
    break;

  case update_dvd_tray_three_state::eject:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);

    /* Check if we should exit this state based on eject flag */
    if (!utils::checkBitNo(flags.bitfield_DATA_73, 1)) {
      /* Eject flag not set - check specific tray positions */
      if (sensors.tray_status == 0x50) {
        /* Tray fully ejected */
        dvd_tray_three = update_dvd_tray_three_state::initial;
        utils::clearBitNo(flags.bitfield_DATA_73, 1);
        return;
      }
    }

    // Check other stable positions
    if (sensors.tray_status == 0x30 || // Fully inserted
        sensors.tray_status == 0x20 || // Position 2
        sensors.tray_status == 0x10) { // Position 1
      utils::clearBitNo(flags.bitfield_DATA_73, 1);
      dvd_tray_three = update_dvd_tray_three_state::wait;
      return;
    }

    // Check if we should wait on timeout
    if (utils::checkBitNo(flags.bitfield_DATA_72, 6)) {
      if (--dvd_tray_timeout == 0) {
        dvd_tray_three = update_dvd_tray_three_state::wait;
        return;
      }
    }
    break;

  default:
    dvd_tray_three = update_dvd_tray_three_state::initial;
    break;
  }
}

} // namespace DVD
} // namespace SMC
