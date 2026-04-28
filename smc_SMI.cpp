#include "smc_SMI.hpp"
#include "smc_audio_clamp.hpp"
#include "smc_dvd.hpp"
#include "smc.hpp"
#include "smc_pll_reset.hpp"
#include "hal.hpp"
#include "utils.hpp"

#include <vector>
#include <string>

namespace SMC {
namespace SMI {

const std::vector<std::string> smi_state_names {
  "Decision_state",
  "Power off start",
  "Process power off conditions",
  "Check ram test results",
  "Interrupt event handled",
  "case5",
  "Request tray close",
  "Leds off",
  "Wait for tray to close",
  "Initial",
  "Overheat cooldown wait",
  "case11",
  "Going to reset",
  "case13",
  "Delay",
  "Wait for stable tray state",
  "Delayed turning off",
  "Wait for power cycle"
};

smi_power_state state;
uint16_t power_timeout2;

void init() {
  state = smi_power_state::decision_state;
}

void setStateCase11() {
  state = smi_power_state::case11;
}

void setStateGoingToReset() {
  state = smi_power_state::going_to_reset;
}

void printState() {
    const size_t smi_state = static_cast<size_t>(state);
    std::string msg = "SMI state [" + std::to_string(smi_state) + "]";

    if (smi_state <= smi_state_names.size()) {
        msg += " " + smi_state_names[smi_state];
    }

    debug::print_message(msg);
}

bool isXboxPowered() {
  return state != smi_power_state::initial;
}

uint8_t update() {
  // debug::print_message("update_SMI_and_power");
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

  switch (state) {
  case smi_power_state::decision_state:
    /* Decision state - Check for pending interrupts */
    clearStatusBit(first_execution);
    flags.bitfield_DATA_70 |= 0x08;
    flags.bitfield_DATA_6F &= ~0x40;
    resetInterruptReason();

    if (checkStatusBit(overheated)) {
      /* System overheated */
      state = smi_power_state::overheat_cooldown_wait;
    } else if (checkStatusBit(power_change_requested)) {
      /* Power button pressed */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_power_sw_pressed);
      fireSystemInterrupt();
      state = smi_power_state::start_power_off;
    } else if (checkStatusBit(eject_change_requested)) {
      /* Eject button pressed */
      clearStatusBit(eject_change_requested);
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_eject_sw_pressed);
      fireSystemInterrupt();
      state = smi_power_state::start_power_off;
    } else if (flags.bitfield_DATA_72 & 0x01) {
      /* AV cable detected */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_power_sw_pressed);
      fireSystemInterrupt();
      state = smi_power_state::start_power_off;
    } else if (flags.bitfield_DATA_71 & 0x04) {
      /* System reset request */
      flags.bitfield_DATA_6F |= 0x40;
      setInterruptReason(InterruptReason_dvd_tray1);
      fireSystemInterrupt();
      state = smi_power_state::process_power_off_conditions;
    } else if (checkStatusBit(dvd_tray)) {
      /* DVD tray change */
      setInterruptReason(InterruptReason_dvd_tray0);
      fireSystemInterrupt();
      state = smi_power_state::process_power_off_conditions;
    } else if (flags.bitfield_DATA_73 & 0x20) {
      /* Boot challenge event */
      setInterruptReason(InterruptReason_dvd_tray2);
      fireSystemInterrupt();
      state = smi_power_state::process_power_off_conditions;
    } else if (checkStatusBit(video_mode_changed)) {
      /* Video mode changed */
      setInterruptReason(InterruptReason_av_mode_changed);
      fireSystemInterrupt();
      state = smi_power_state::process_power_off_conditions;
    } else if (flags.bitfield_DATA_71 & 0x80) {
      /* No AV cable */
      setInterruptReason(InterruptReason_av_unplugged);
      fireSystemInterrupt();
      state = smi_power_state::process_power_off_conditions;
    }
    break;

  case smi_power_state::start_power_off:
    flags.bitfield_DATA_70 &= ~0x08;
    if (flags.bitfield_DATA_72 & 0x20) {
      /* FRAG set - go to different state */
      state = smi_power_state::leds_off;
    } else {
      state = smi_power_state::process_power_off_conditions;
    }
    break;

  case smi_power_state::process_power_off_conditions:
    timers.power_timeout = 25;
    power_timeout2 = 3;

    if (checkStatusBit(power_change_requested)) {
      state = smi_power_state::check_ram_test_results;
    } else if (flags.bitfield_DATA_72 & 0x01) {
      state = smi_power_state::check_ram_test_results;
    } else if (flags.bitfield_DATA_71 & 0x04) {
      if (flags.bitfield_DATA_72 & 0x02) {
        state = smi_power_state::case11;
      } else {
        state = smi_power_state::case11;
      }
    } else if (checkStatusBit(dvd_tray)) {
      state = smi_power_state::case11;
    } else if (flags.bitfield_DATA_73 & 0x20) {
      state = smi_power_state::case11;
    } else if (flags.bitfield_DATA_71 & 0x80) {
      state = smi_power_state::case11;
    } else if (checkStatusBit(video_mode_changed)) {
      state = smi_power_state::case11;
    } else {
      state = smi_power_state::case11;
    }
    break;

  case smi_power_state::check_ram_test_results:
    if ((flags.bitfield_DATA_6F & 0x02) == 0) {
      if ((flags.bitfield_DATA_71 & 0x10) == 0) {
        state = smi_power_state::request_tray_close;
      } else if ((flags.bitfield_DATA_71 & 0x20) == 0) {
        state = smi_power_state::request_tray_close;
      } else if ((flags.bitfield_DATA_72 & 0x40) == 0) {
        state = smi_power_state::request_tray_close;
      } else if (flags.bitfield_DATA_70 & 0x20) {
        state = smi_power_state::case13;
      } else {
        if (--timers.power_timeout == 0) {
          state = smi_power_state::request_tray_close;
        }
      }
    } else {
      state = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::event_interrupt_handled:
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
    state = smi_power_state::decision_state;
    break;

  case smi_power_state::case5:
    /* Intermediate state */
    timers.power_timeout = 0xFF;
    flags.bitfield_DATA_70 &= ~0x20;
    flags.bitfield_DATA_6F |= 0x40;
    state = smi_power_state::check_ram_test_results;
    break;

  case smi_power_state::request_tray_close:
    setStatusBit(prepare_for_shutdown); // System shutting down
    timers.power_timeout = 25;
    state = smi_power_state::wait_tray_close;
    break;

  case smi_power_state::leds_off:
    resetInterruptReason();
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 &= ~0x02;
    flags.bitfield_DATA_6F &= ~0x02;
    flags.bitfield_DATA_70 &= ~0x20;
    flags.bitfield_DATA_6F &= ~0x40;
    timers.power_timeout = 25;
    state = smi_power_state::delay;
    break;

  case smi_power_state::wait_tray_close:
    if (AudioClamp::isClamped() && Dvd::isTrayClosing()) {
      timers.power_timeout = 0xFF;
      state = smi_power_state::wait_for_tray_stable;
    } else if (--timers.power_timeout == 0) {
      timers.power_timeout = 0xFF;
      state = smi_power_state::wait_for_tray_stable;
    }
    break;

  case smi_power_state::initial:
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
      state = smi_power_state::wait_state_for_power_cycle;
    } else if ((checkStatusBit(first_execution)) == 0) {
      return 1; /* System powered off */
    }
    state = smi_power_state::decision_state;
    clearStatusBit(prepare_for_shutdown);

    return 1;
    break;

  case smi_power_state::overheat_cooldown_wait:
    if ((checkStatusBit(overheated)) == 0) {
      timers.power_timeout = 1;
      state = smi_power_state::delay;
    }
    break;

  case smi_power_state::case11:
    /* Check for various conditions before going to state 4 or others */
    if (flags.bitfield_DATA_70 & 0x10) {
      if ((flags.bitfield_DATA_71 & 0x04) == 0) {
        state = smi_power_state::event_interrupt_handled;
      } else {
        state = smi_power_state::event_interrupt_handled;
      }
    } else if (flags.bitfield_DATA_70 & 0x20) {
      state = smi_power_state::case5;
    } else if (flags.bitfield_DATA_6F & 0x02) {
      state = smi_power_state::request_tray_close;
    } else if (flags.bitfield_DATA_71 & 0x08) {
      state = smi_power_state::going_to_reset;
    } else {
      if (--timers.power_timeout == 0) {
        state = smi_power_state::going_to_reset;
      }
    }
    break;

  case smi_power_state::going_to_reset:
    flags.bitfield_DATA_71 &= ~0x08;
    flags.bitfield_DATA_73 |= 0x01; // Set reset request
    clearStatusBit(video_mode_changed);
    flags.bitfield_DATA_71 &= ~0x80;
    clearStatusBit(dvd_tray);
    flags.bitfield_DATA_71 &= ~0x04;
    state = smi_power_state::decision_state;
    break;

  case smi_power_state::case13:
    /* Intermediate handling state */
    if (--power_timeout2 != 0) {
      state = smi_power_state::case5;
    } else {
      state = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::delay:
    if (PLL_Reset::isState1()) {
      if (--timers.power_timeout == 0) {
        /* Proceed */
      }
    } else if (--timers.power_timeout == 0) {
      /* Proceed */
    }
    break;

  case smi_power_state::wait_for_tray_stable:
    sensors.tray_status = hal::get_tray_state();

    if ((sensors.tray_status == 0x00) || (sensors.tray_status == 0x40) || (sensors.tray_status == 0x60)) {
      state = smi_power_state::leds_off;
    } else if (--timers.power_timeout == 0) {
      state = smi_power_state::leds_off;
    }
    break;

  case smi_power_state::delayed_turning_off:
    /* Delayed turning power off */
    if (--timers.power_timeout == 0) {
      hal::turn_off_psu();
      state = smi_power_state::initial;
    }
    break;

  case smi_power_state::wait_state_for_power_cycle:
    if (--timers.power_timeout == 0) {
      state = smi_power_state::initial;
    }
    break;

  default:
    break;
  }

  return 0; /* System powered on */
}

} // namespace SMI
} // namespace SMC
