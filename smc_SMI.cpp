#include "smc.hpp"
#include "pico_hal.hpp"
#include "utils.hpp"

namespace SMC {

uint8_t update_SMI_and_power() {
  debug::print_message("update_SMI_and_power");
  // Manages power interrupts and SMI signaling when Xbox is powered up

  // State starts at 9 when powered on, and stops at 9 when powered off
  // State transitions:
  // 0 -> (1, 2, 10)
  // 1 -> (2, 7)
  // 2 -> (3, 4, 11)
  // 3 -> (6, 13)
  // 4 -> 0
  // 5 -> 3
  // 6 -> 8 -> 15 -> 7 -> 14 -> 16 -> 9
  // 9 -> (0, 17)
  // 10 -> (10, 14)
  // 11 -> (4, 5, 6, 12)
  // 12 -> 0
  // 13 -> (5, 6)
  // 17 -> 9

  switch (state.smi_power) {
  case smi_power_state::decision_state:
    /* Decision state - Check for pending interrupts */
    clearStatusBit(first_execution);
    flags.bitfield_DATA_70 |= 0x08;
    flags.bitfield_DATA_6F &= ~0x40;
    resetInterruptReason();

    if (checkStatusBit(overheated)) {
      /* System overheated */
      state.smi_power = smi_power_state::overheat_cooldown_wait;
    } else if (checkStatusBit(power_change_requested)) {
      /* Power button pressed */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_power_sw_pressed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::start_power_off;
    } else if (checkStatusBit(eject_change_requested)) {
      /* Eject button pressed */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_eject_sw_pressed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::start_power_off;
    } else if (flags.bitfield_DATA_72 & 0x01) {
      /* AV cable detected */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_power_sw_pressed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::start_power_off;
    } else if (flags.bitfield_DATA_71 & 0x04) {
      /* System reset request */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_dvd_tray1);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (checkStatusBit(dvd_tray)) {
      /* DVD tray change */
      setInterruptReason(InterruptReason_dvd_tray0);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (flags.bitfield_DATA_73 & 0x20) {
      /* Boot challenge event */
      setInterruptReason(InterruptReason_dvd_tray2);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (checkStatusBit(video_mode_changed)) {
      /* Video mode changed */
      setInterruptReason(InterruptReason_av_mode_changed);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    } else if (flags.bitfield_DATA_71 & 0x80) {
      /* No AV cable */
      setInterruptReason(InterruptReason_av_unplugged);
      fireSystemInterrupt();
      state.smi_power = smi_power_state::case2;
    }
    break;

  case smi_power_state::start_power_off:
    /* Starting to power off */
    flags.bitfield_DATA_70 &= ~0x08;
    if (flags.bitfield_DATA_72 & 0x20) {
      /* FRAG set - go to different state */
      state.smi_power = smi_power_state::leds_off;
    } else {
      state.smi_power = smi_power_state::case2;
    }
    break;

  case smi_power_state::case2:
    /* Process power-off conditions */
    timers.power_timeout = 25;
    timers.power_timeout2 = 3;

    if (checkStatusBit(power_change_requested)) {
      state.smi_power = smi_power_state::case3;
    } else if (flags.bitfield_DATA_72 & 0x01) {
      state.smi_power = smi_power_state::case3;
    } else if (flags.bitfield_DATA_71 & 0x04) {
      if (flags.bitfield_DATA_72 & 0x02) {
        state.smi_power = smi_power_state::case11;
      } else {
        state.smi_power = smi_power_state::case11;
      }
    } else if (checkStatusBit(dvd_tray)) {
      state.smi_power = smi_power_state::case11;
    } else if (flags.bitfield_DATA_73 & 0x20) {
      state.smi_power = smi_power_state::case11;
    } else if (flags.bitfield_DATA_71 & 0x80) {
      state.smi_power = smi_power_state::case11;
    } else if (checkStatusBit(video_mode_changed)) {
      state.smi_power = smi_power_state::case11;
    } else {
      state.smi_power = smi_power_state::case11;
    }
    break;

  case smi_power_state::case3:
    /* Check RAM test results and proceed */
    if ((flags.bitfield_DATA_6F & 0x02) == 0) {
      if ((flags.bitfield_DATA_71 & 0x10) == 0) {
        state.smi_power = smi_power_state::request_tray_close;
      } else if ((flags.bitfield_DATA_71 & 0x20) == 0) {
        state.smi_power = smi_power_state::request_tray_close;
      } else if ((flags.bitfield_DATA_72 & 0x40) == 0) {
        state.smi_power = smi_power_state::request_tray_close;
      } else if (flags.bitfield_DATA_70 & 0x20) {
        state.smi_power = smi_power_state::case13;
      } else {
        if (--timers.power_timeout == 0) {
          state.smi_power = smi_power_state::request_tray_close;
        }
      }
    } else {
      state.smi_power = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::event_interrupt_handled:
    /* Event/interrupt handled */
    if (flags.interrupt_reason & 0x08) {
      clearStatusBit(video_mode_changed);
    }
    if (flags.interrupt_reason & 0x10) {
      flags.bitfield_DATA_71 &= ~0x80;
    }
    if (flags.interrupt_reason & 0x02) {
      clearStatusBit(dvd_tray);
    }
    if (flags.interrupt_reason & 0x40) {
      flags.bitfield_DATA_73 &= ~0x20;
    }
    if (flags.interrupt_reason & 0x04) {
      flags.bitfield_DATA_71 &= ~0x04;
    }
    flags.bitfield_DATA_70 &= ~0x10;
    flags.bitfield_DATA_71 &= ~0x08;
    state.smi_power = smi_power_state::decision_state;
    break;

  case smi_power_state::case5:
    /* Intermediate state */
    timers.power_timeout = 0xFF;
    flags.bitfield_DATA_70 &= ~0x20;
    flags.bitfield_DATA_6F |= 0x40;
    state.smi_power = smi_power_state::case3;
    break;

  case smi_power_state::request_tray_close:
    /* Request tray close and set timeout */
    setStatusBit(prepare_for_shutdown); // System shutting down
    timers.power_timeout = 25;
    state.smi_power = smi_power_state::wait_tray_close;
    break;

  case smi_power_state::leds_off:
    /* LEDs off */
    resetInterruptReason();
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 &= ~0x02;
    flags.bitfield_DATA_6F &= ~0x02;
    flags.bitfield_DATA_70 &= ~0x20;
    flags.bitfield_DATA_6F &= ~0x40;
    timers.power_timeout = 25;
    state.smi_power = smi_power_state::delay;
    break;

  case smi_power_state::wait_tray_close:
    /* Wait for tray to close */
    if (state.audio_clamp == audio_state::clamped && state.tray_eject == 1) {
      timers.power_timeout = 0xFF;
      state.smi_power = smi_power_state::case15;
    } else if (--timers.power_timeout == 0) {
      timers.power_timeout = 0xFF;
      state.smi_power = smi_power_state::case15;
    }
    break;

  case smi_power_state::initial:
    /* Initial state - system idle */
    flags.bitfield_DATA_72 &= ~0x20;
    flags.bitfield_DATA_70 &= ~0x08;
    clearStatusBit(video_mode_changed);
    flags.bitfield_DATA_71 &= ~0x80;
    clearStatusBit(dvd_tray);
    flags.bitfield_DATA_71 &= ~0x04;
    clearStatusBit(power_change_requested);
    flags.bitfield_DATA_72 &= ~0x01;

    if (flags.bitfield_DATA_73 & 0x40) {
      /* Power cycle requested */
      flags.bitfield_DATA_73 &= ~0x40;
      state.smi_power = smi_power_state::wait_state_for_initial;
    } else if ((checkStatusBit(first_execution)) == 0) {
      return 1; /* System powered off */
    }
    state.smi_power = smi_power_state::decision_state;
    clearStatusBit(prepare_for_shutdown);

    return 1;
    break;

  case smi_power_state::overheat_cooldown_wait:
    /* Wait for overheated status to end */
    if ((checkStatusBit(overheated)) == 0) {
      timers.power_timeout = 1;
      state.smi_power = smi_power_state::delay;
    }
    break;

  case smi_power_state::case11:
    /* Check for various conditions before going to state 4 or others */
    if (flags.bitfield_DATA_70 & 0x10) {
      if ((flags.bitfield_DATA_71 & 0x04) == 0) {
        state.smi_power = smi_power_state::event_interrupt_handled;
      } else {
        state.smi_power = smi_power_state::event_interrupt_handled;
      }
    } else if (flags.bitfield_DATA_70 & 0x20) {
      state.smi_power = smi_power_state::case5;
    } else if (flags.bitfield_DATA_6F & 0x02) {
      state.smi_power = smi_power_state::request_tray_close;
    } else if (flags.bitfield_DATA_71 & 0x08) {
      state.smi_power = smi_power_state::going_to_reset;
    } else {
      if (--timers.power_timeout == 0) {
        state.smi_power = smi_power_state::going_to_reset;
      }
    }
    break;

  case smi_power_state::going_to_reset:
    /* Going to reset */
    flags.bitfield_DATA_71 &= ~0x08;
    flags.bitfield_DATA_73 |= 0x01; // Set reset request
    clearStatusBit(video_mode_changed);
    flags.bitfield_DATA_71 &= ~0x80;
    clearStatusBit(dvd_tray);
    flags.bitfield_DATA_71 &= ~0x04;
    state.smi_power = smi_power_state::decision_state;
    break;

  case smi_power_state::case13:
    /* Intermediate handling state */
    if (--timers.power_timeout2 != 0) {
      state.smi_power = smi_power_state::case5;
    } else {
      state.smi_power = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::delay:
    /* Delay state */
    if (state.pll_reset == pll_sysreset_state::state1) {
      if (--timers.power_timeout == 0) {
        /* Proceed */
      }
    } else if (--timers.power_timeout == 0) {
      /* Proceed */
    }
    break;

  case smi_power_state::case15:
    /* Get tray status and wait for stable state */
    sensors.tray_status = pico_hal::get_tray_state();

    if ((sensors.tray_status == 0x00) || (sensors.tray_status == 0x40) || (sensors.tray_status == 0x60)) {
      state.smi_power = smi_power_state::leds_off;
    } else if (--timers.power_timeout == 0) {
      state.smi_power = smi_power_state::leds_off;
    }
    break;

  case smi_power_state::delayed_turning_off:
    /* Delayed turning power off */
    if (--timers.power_timeout == 0) {
      pico_hal::turn_off_psu();
      state.smi_power = smi_power_state::initial;
    }
    break;

  case smi_power_state::wait_state_for_initial:
    /* Wait state for power cycle */
    if (--timers.power_timeout == 0) {
      state.smi_power = smi_power_state::initial;
    }
    break;

  default:
    break;
  }

  return 0; /* System powered on */
}

} // namespace SMC
