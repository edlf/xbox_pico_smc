#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"

namespace SMC {

uint8_t raw_tray_status_filtered;

void update_dvd_tray() {
  // Done, untested
  uint8_t tray_state;

  switch (state.dvd_tray) {
  case dvd_tray_state::initial:
    timers.tray_state_timer = 0x20;
    state.dvd_tray = dvd_tray_state::tick_timer;
    break;

  case dvd_tray_state::tick_timer:
    timers.tray_state_timer--;
    if (timers.tray_state_timer == 0) {
      state.dvd_tray = dvd_tray_state::state2;
    }
    break;

  case dvd_tray_state::state2:
    sensors.tray_status_raw = pico_hal::get_tray_state() & 0x70;
    raw_tray_status_filtered = sensors.tray_status_raw;
    if ((sensors.tray_status ^ sensors.tray_status_raw) != 0) {
      state.dvd_tray = dvd_tray_state::state3;
    }
    break;

  case dvd_tray_state::state3:
    tray_state = pico_hal::get_tray_state();
    sensors.tray_status_raw = tray_state & 0x70;
    if ((tray_state & 0x70) != raw_tray_status_filtered) {
      state.dvd_tray = dvd_tray_state::state2;
    } else {
      state.dvd_tray = dvd_tray_state::state4;
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
        state.dvd_tray = dvd_tray_state::state2;
        utils::setBitNo(flags.bitfield_DATA_72, 4);
        return;
      }
      if (utils::checkBitNo(flags.bitfield_DATA_72, 4) != 0) {
        setStatusBit(InterruptReason_eject_sw_pressed);
      }
    }
    state.dvd_tray = dvd_tray_state::state2;
    return;
    break;

  default:
    state.dvd_tray = dvd_tray_state::initial;
    break;
  }
}

void update_eject_tray() {
  // Done, not tested

  switch (state.update_eject_tray) {
  case update_eject_tray_state::initial:
    timers.eject_timeout = 5;

    // Check if eject signal is set
    if (utils::checkBitNo(flags.bitfield_DATA_72, 5) != 0) {
      state.update_eject_tray = update_eject_tray_state::tick_timer;
    }
    break;

  case update_eject_tray_state::tick_timer:
    pico_hal::dvd_eject_off();
    if (--timers.eject_timeout == 0) {
      state.update_eject_tray = update_eject_tray_state::release_eject;
    }
    break;

  case update_eject_tray_state::release_eject:
    utils::clearBitNo(flags.bitfield_DATA_72, 5);
    pico_hal::dvd_eject_on();
    state.update_eject_tray = update_eject_tray_state::initial;
    break;

  default:
    state.update_eject_tray = update_eject_tray_state::initial;
    break;
  }
}

void update_dvd_tray_eject() {
  /* Handle tray eject mechanism - manages DVD tray eject/inject operations
   * States: 0=check eject conditions, 1=wait for shutdown signal */

  switch (state.tray_eject) {
  case 0:
    /* Check if we should initiate tray eject */

    /* If tray is already at state 0x50 (fully ejected), nothing to do */
    if (sensors.tray_status == 0x50) {
      /* Tray already ejected - clear flags and return to state 1 */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F &= ~0x10; // Clear eject request
      state.tray_eject = 1;
      return;
    }

    /* Check if eject should be triggered */
    if ((checkStatusBit(eject_change_requested)) || // Eject button pressed
        (flags.bitfield_DATA_6F & 0x10)) {          // Eject request from elsewhere

      /* Check current tray state via PORTB bits 4-6 */
      uint8_t tray_state = pico_hal::get_tray_state();

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
    state.tray_eject = 1;
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
      state.tray_eject = 0;
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
      uint8_t tray_state = pico_hal::get_tray_state();

      /* Only proceed if not already in eject/insert states */
      if (tray_state != 0x70 && tray_state != 0x20) {
        flags.bitfield_DATA_72 |= 0x08; // Signal eject
      }
    }
    break;

  default:
    state.tray_eject = 0;
    break;
  }
}

void update_dvd_tray3() {
  // Done, untested
  switch (state.dvd_tray_3) {
  case update_dvd_tray3::initial:
    timers.dvd_tray_timeout = 0xFF;
    utils::clearBitNo(flags.bitfield_DATA_6F, 5);

    if (utils::checkBitNo(flags.bitfield_DATA_72, 7)) {
      state.dvd_tray_3 = update_dvd_tray3::eject;
    } else {
      if (!(sensors.tray_status == 0x70 ||  // Eject state
            sensors.tray_status == 0x10 ||  // State 1
            sensors.tray_status == 0x00 ||  // State 0
            sensors.tray_status == 0x40 ||  // State 4
            sensors.tray_status == 0x60)) { // State 6
        state.dvd_tray_3 = update_dvd_tray3::wait;
      }
    }
    break;

  case update_dvd_tray3::wait:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);
    utils::clearBitNo(flags.bitfield_DATA_72, 7);

    // Check if tray reached a stable position
    if (sensors.tray_status == 0x10 || // Stable position 1
        sensors.tray_status == 0x40 || // Stable position 4
        sensors.tray_status == 0x60) { // Stable position 6
      state.dvd_tray_3 = update_dvd_tray3::initial;
    }
    break;

  case update_dvd_tray3::eject:
    utils::setBitNo(flags.bitfield_DATA_6F, 5);

    /* Check if we should exit this state based on eject flag */
    if (!utils::checkBitNo(flags.bitfield_DATA_73, 1)) {
      /* Eject flag not set - check specific tray positions */
      if (sensors.tray_status == 0x50) {
        /* Tray fully ejected */
        state.dvd_tray_3 = update_dvd_tray3::initial;
        utils::clearBitNo(flags.bitfield_DATA_73, 1);
        return;
      }
    }

    // Check other stable positions
    if (sensors.tray_status == 0x30 || // Fully inserted
        sensors.tray_status == 0x20 || // Position 2
        sensors.tray_status == 0x10) { // Position 1
      utils::clearBitNo(flags.bitfield_DATA_73, 1);
      state.dvd_tray_3 = update_dvd_tray3::wait;
      return;
    }

    // Check if we should wait on timeout
    if (utils::checkBitNo(flags.bitfield_DATA_72, 6)) {
      if (--timers.dvd_tray_timeout == 0) {
        state.dvd_tray_3 = update_dvd_tray3::wait;
        return;
      }
    }
    break;

  default:
    state.dvd_tray_3 = update_dvd_tray3::initial;
    break;
  }
}

} // namespace SMC
