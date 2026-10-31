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
uint16_t power_timeout = 0;
uint16_t power_timeout2 = 0;
static bool power_on_requested = false; // 0xBD.0, survives init()
static volatile uint8_t power_cycle_delay = 25; // P11: SMBus 0x22, 0xBE on the PIC

void init() {
  state = smi_power_state::initial; // Same as the PIC, goes to decision_state once powered up
  power_timeout = 0;
  power_timeout2 = 0;
  power_cycle_delay = 25; // Reset by RAM_init on P11 (0xB5A)
}

bool takePowerOnRequest() {
  const bool requested = power_on_requested;
  power_on_requested = false;
  return requested;
}

bool powerOnRequested() {
  return power_on_requested;
}

void setPowerCycleDelay(uint8_t ticks) {
  power_cycle_delay = ticks;
}

void printState() {
    const size_t smi_state = static_cast<size_t>(state);
    std::string msg = "SMI state [" + std::to_string(smi_state) + "]";

    if (smi_state <= smi_state_names.size()) {
        msg += " " + smi_state_names[smi_state];
    }

    debug::print_message(msg);
}

// Latches the reason in both the SMBus-readable byte (6C) and the pending copy
// (6D) that event_interrupt_handled uses to clear the source, then pulses SMI
static void raiseInterrupt(uint8_t reason) {
  setInterruptReason(reason);
  flags.pending_interrupt_reason |= reason;
  fireSystemInterrupt();
}

uint8_t update() {
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
    flags.system_running = true;
    flags.waiting_for_os = false;
    flags.pending_interrupt_reason = 0; // Pending copy only, 6C is cleared by SMBus read 0x11

    if (checkStatusBit(overheated)) {
      /* System overheated */
      state = smi_power_state::overheat_cooldown_wait;
    } else if (checkStatusBit(power_change_requested)) {
      /* Power button pressed */
      flags.waiting_for_os = true;
      raiseInterrupt(InterruptReason_power_sw_pressed);
      state = smi_power_state::start_power_off;
    } else if (checkStatusBit(eject_change_requested)) {
      /* Eject button pressed */
      clearStatusBit(eject_change_requested);
      flags.waiting_for_os = true;
      raiseInterrupt(InterruptReason_eject_sw_pressed);
      state = smi_power_state::process_power_off_conditions;
    } else if (flags.av_power_button_event) {
      /* Special AV mode: same as the power button */
      flags.waiting_for_os = true;
      raiseInterrupt(InterruptReason_power_sw_pressed);
      state = smi_power_state::start_power_off;
    } else if (flags.tray_opening_event) {
      /* DVD tray event (0x30) */
      if (!flags.reset_on_eject_disabled) {
        flags.waiting_for_os = true;
      }
      raiseInterrupt(InterruptReason_dvd_tray1);
      state = smi_power_state::process_power_off_conditions;
    } else if (checkStatusBit(dvd_tray)) {
      /* DVD tray change */
      raiseInterrupt(InterruptReason_dvd_tray0);
      state = smi_power_state::process_power_off_conditions;
    } else if (flags.tray_state_00_event) {
      /* DVD tray event (0x00) */
      raiseInterrupt(InterruptReason_dvd_tray2);
      state = smi_power_state::process_power_off_conditions;
    } else if (checkStatusBit(video_mode_changed)) {
      /* Video mode changed */
      raiseInterrupt(InterruptReason_av_mode_changed);
      state = smi_power_state::process_power_off_conditions;
    } else if (flags.av_unplugged_event) {
      /* No AV cable */
      raiseInterrupt(InterruptReason_av_unplugged);
      state = smi_power_state::process_power_off_conditions;
    }
    break;

  case smi_power_state::start_power_off:
    flags.system_running = false;
    if (flags.frag_power_off) {
      /* FRAG set - go to different state */
      state = smi_power_state::leds_off;
    } else {
      state = smi_power_state::process_power_off_conditions;
    }
    break;

  case smi_power_state::process_power_off_conditions:
    power_timeout = 25;
    power_timeout2 = 3;

    if (checkStatusBit(power_change_requested) || flags.av_power_button_event) {
      state = smi_power_state::check_ram_test_results;
    } else if (flags.tray_opening_event) {
      // Tray event resets the Xbox unless reset-on-eject is disabled
      if (flags.reset_on_eject_disabled) {
        state = smi_power_state::event_interrupt_handled;
      } else {
        state = smi_power_state::case11;
      }
    } else if (checkStatusBit(dvd_tray) || flags.tray_state_00_event ||
               flags.av_unplugged_event || checkStatusBit(video_mode_changed)) {
      // Plain notifications, nothing else to do once the SMI has been sent
      state = smi_power_state::event_interrupt_handled;
    } else {
      state = smi_power_state::case11;
    }
    break;

  case smi_power_state::check_ram_test_results:
    if (!flags.power_off_request) {
      if (!flags.ram_test_submitted) {
        state = smi_power_state::request_tray_close;
      } else if (!flags.ram_test_passed) {
        state = smi_power_state::request_tray_close;
      } else if (!flags.os_ready) {
        state = smi_power_state::request_tray_close;
      } else if (flags.os_busy) {
        state = smi_power_state::case13;
      } else {
        if (--power_timeout == 0) {
          state = smi_power_state::request_tray_close;
        }
      }
    } else {
      state = smi_power_state::request_tray_close;
    }
    break;

  case smi_power_state::event_interrupt_handled:
    // Use the pending copy, the host may already have read and cleared 6C
    if (flags.pending_interrupt_reason & InterruptReason_av_mode_changed) {
      clearStatusBit(video_mode_changed);
    }
    if (flags.pending_interrupt_reason & InterruptReason_av_unplugged) {
      flags.av_unplugged_event = false;
    }
    if (flags.pending_interrupt_reason & InterruptReason_dvd_tray0) {
      clearStatusBit(dvd_tray);
    }
    if (flags.pending_interrupt_reason & InterruptReason_dvd_tray2) {
      flags.tray_state_00_event = false;
    }
    if (flags.pending_interrupt_reason & InterruptReason_dvd_tray1) {
      flags.tray_opening_event = false;
    }
    flags.os_event_handled = false;
    flags.os_reset_request = false;
    state = smi_power_state::decision_state;
    break;

  case smi_power_state::case5:
    /* Intermediate state */
    power_timeout = 0xFF;
    flags.os_busy = false;
    flags.waiting_for_os = true;
    state = smi_power_state::check_ram_test_results;
    break;

  case smi_power_state::request_tray_close:
    setStatusBit(prepare_for_shutdown); // System shutting down
    power_timeout = 25;
    state = smi_power_state::wait_tray_close;
    break;

  case smi_power_state::leds_off:
    Led::clearPhases();
    flags.cold_reset_request = true;
    flags.run_after_reset = false;
    flags.power_off_request = false;
    flags.os_busy = false;
    flags.waiting_for_os = false;
    power_timeout = 25;
    state = smi_power_state::delay;
    break;

  case smi_power_state::wait_tray_close:
    if (AudioClamp::isClamped() && Dvd::isTrayClosing()) {
      power_timeout = 0xFF;
      state = smi_power_state::wait_for_tray_stable;
    } else if (--power_timeout == 0) {
      power_timeout = 0xFF;
      state = smi_power_state::wait_for_tray_stable;
    }
    break;

  case smi_power_state::initial:
    flags.frag_power_off = false;
    flags.system_running = false;
    clearStatusBit(video_mode_changed);
    flags.av_unplugged_event = false;
    clearStatusBit(dvd_tray);
    flags.tray_opening_event = false;
    clearStatusBit(power_change_requested);
    flags.av_power_button_event = false;

    if (flags.power_cycle_request) {
      flags.power_cycle_request = false;
#if SMC_FIRMWARE_P11
      /* Power cycle requested (SMBus 0x02 bit 6): power back on after the
       * SMBus 0x22 delay minus 2 ticks (0x7E1). Like the PIC's DECFSZ, a
       * result of 0 counts 256 ticks */
      power_timeout = (uint8_t)(power_cycle_delay - 2);
      if (power_timeout == 0) {
        power_timeout = 256;
      }
#else
      /* Power cycle requested (SMBus 0x02 bit 6): power back on in 50 ticks */
      power_timeout = 0x32;
#endif
      state = smi_power_state::wait_state_for_power_cycle;
    } else if (checkStatusBit(first_execution)) {
      /* Just powered up */
      state = smi_power_state::decision_state;
      clearStatusBit(prepare_for_shutdown);
    } else {
      return 1; /* Powered off */
    }
    break;

  case smi_power_state::overheat_cooldown_wait:
    if ((checkStatusBit(overheated)) == 0) {
      power_timeout = 1;
      state = smi_power_state::delay;
    }
    break;

  case smi_power_state::case11:
    /* Check for various conditions before going to state 4 or others */
    if (flags.os_event_handled) {
      /* OS handled it (SMBus 0x0D), unless it's a tray event with
       * reset-on-eject enabled: that one still resets after the timeout */
      if (flags.tray_opening_event && !flags.reset_on_eject_disabled) {
        if (--power_timeout == 0) {
          state = smi_power_state::going_to_reset;
        }
      } else {
        state = smi_power_state::event_interrupt_handled;
      }
    } else if (flags.os_busy) {
      state = smi_power_state::case5;
    } else if (flags.power_off_request) {
      state = smi_power_state::request_tray_close;
    } else if (flags.os_reset_request) {
      state = smi_power_state::going_to_reset;
    } else {
      if (--power_timeout == 0) {
        state = smi_power_state::going_to_reset;
      }
    }
    break;

  case smi_power_state::going_to_reset:
    flags.os_reset_request = false;
    flags.warm_reset_request = true;
    clearStatusBit(video_mode_changed);
    flags.av_unplugged_event = false;
    clearStatusBit(dvd_tray);
    flags.tray_opening_event = false;
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
    // Proceed at once if the PLL is already off, otherwise after the timeout
    if (!PLL_Reset::isState1() && --power_timeout != 0) {
      break;
    }

    // Stop watching POWOK before dropping POWON, so the expected power loss
    // doesn't trigger the power-failure handler
    setPowerOkIrq(false);
    power_timeout = 2;
    state = smi_power_state::delayed_turning_off;
    break;

  case smi_power_state::wait_for_tray_stable:
    sensors.tray_status = Dvd::readTrayState();

    if ((sensors.tray_status == 0x00) || (sensors.tray_status == 0x40) || (sensors.tray_status == 0x60)) {
      state = smi_power_state::leds_off;
    } else if (--power_timeout == 0) {
      state = smi_power_state::leds_off;
    }
    break;

  case smi_power_state::delayed_turning_off:
    /* Delayed turning power off */
    if (--power_timeout == 0) {
      hal::turn_off_psu();
      state = smi_power_state::initial;
    }
    break;

  case smi_power_state::wait_state_for_power_cycle:
    if (--power_timeout == 0) {
      power_on_requested = true;
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
