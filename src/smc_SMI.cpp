#include "smc_SMI.hpp"
#include "smc_audio_clamp.hpp"
#include "smc_dvd.hpp"
#include "smc.hpp"
#include "smc_pll_reset.hpp"
#include "hal.hpp"

#include <cstdio>
#include <iterator>
#include <string>

namespace SMC {
namespace SMI {

flags_struct flags;

constexpr const char* smi_state_names[] = {"Decision_state",
                                           "Power off start",
                                           "Process power off conditions",
                                           "Wait for OS power off",
                                           "Interrupt event handled",
                                           "OS busy, wait longer",
                                           "Request tray close",
                                           "Leds off",
                                           "Wait for tray to close",
                                           "Initial",
                                           "Overheat cooldown wait",
                                           "Wait for OS response",
                                           "Going to reset",
                                           "OS busy, check limit",
                                           "Delay",
                                           "Wait for stable tray state",
                                           "Delayed turning off",
                                           "Wait for power cycle"};

smi_power_state         state;
uint16_t                power_timeout = 0;
uint16_t                power_timeout2 = 0;          // How many more times the OS may answer "busy"
static bool             power_on_requested = false;  // Survives init()
static volatile uint8_t power_cycle_delay = 25;      // SMBus 0x22

void init() {
  state = smi_power_state::initial;  // Same as the PIC, goes to decision_state once powered up
  power_timeout = 0;
  power_timeout2 = 0;
  power_cycle_delay = 25;  // Reset by RAM_init (P11)
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
  std::string  msg = "SMI state [" + std::to_string(smi_state) + "]";

  if (smi_state < std::size(smi_state_names)) {
    msg += std::string(" ") + smi_state_names[smi_state];
  }

  debug::print_message(msg);
}

void resetInterruptReason() {
  flags.interrupt_reason = 0;
}

static void setInterruptReason(uint8_t ir) {
  flags.interrupt_reason |= ir;
}

static void fireSystemInterrupt() {
  debug::print_message("SMC: Fire System Interrupt");

  // Pulse SMI signal (RA4) low to signal system interrupt. The PIC holds it
  // low for 5 instruction cycles (2 us @ 10 MHz)
  hal::smi_pin_off();
  busy_wait_us(2);
  hal::smi_pin_on();
}

// Latches the reason in both interrupt_reason (read by SMBus 0x11) and the
// pending copy that event_interrupt_handled uses to clear the source, then
// pulses SMI
void raiseInterrupt(uint8_t reason) {
  char msg[32];
  snprintf(msg, sizeof(msg), "SMI: Interrupt reason 0x%02X", reason);
  debug::print_message(msg);
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
      // Decision state - Check for pending interrupts
      flags.first_execution = false;
      flags.system_running = true;
      flags.waiting_for_os = false;
      flags.pending_interrupt_reason = 0;  // Pending copy only, interrupt_reason is cleared by SMBus read 0x11

      if (Fan::flags.overheated) {
        // System overheated
        state = smi_power_state::overheat_cooldown_wait;
      } else if (flags.power_change_requested) {
        // Power button pressed
        flags.waiting_for_os = true;
        raiseInterrupt(InterruptReason_power_sw_pressed);
        state = smi_power_state::start_power_off;
      } else if (Dvd::flags.eject_change_requested) {
        // Eject button pressed
        Dvd::flags.eject_change_requested = false;
        flags.waiting_for_os = true;
        raiseInterrupt(InterruptReason_eject_sw_pressed);
        state = smi_power_state::process_power_off_conditions;
      } else if (flags.av_power_button_event) {
        // Special AV mode: same as the power button
        flags.waiting_for_os = true;
        raiseInterrupt(InterruptReason_power_sw_pressed);
        state = smi_power_state::start_power_off;
      } else if (flags.tray_opening_event) {
        // DVD tray event (0x30)
        if (!flags.reset_on_eject_disabled) {
          flags.waiting_for_os = true;
        }
        raiseInterrupt(InterruptReason_dvd_tray1);
        state = smi_power_state::process_power_off_conditions;
      } else if (flags.tray_media_event) {
        // DVD tray change
        raiseInterrupt(InterruptReason_dvd_tray0);
        state = smi_power_state::process_power_off_conditions;
      } else if (flags.tray_state_00_event) {
        // DVD tray event (0x00)
        raiseInterrupt(InterruptReason_dvd_tray2);
        state = smi_power_state::process_power_off_conditions;
      } else if (flags.video_mode_changed) {
        // Video mode changed
        raiseInterrupt(InterruptReason_av_mode_changed);
        state = smi_power_state::process_power_off_conditions;
      } else if (flags.av_unplugged_event) {
        // No AV cable
        raiseInterrupt(InterruptReason_av_unplugged);
        state = smi_power_state::process_power_off_conditions;
      }
      break;

    case smi_power_state::start_power_off:
      flags.system_running = false;
      if (BootChallenge::flags.frag_power_off) {
        // FRAG set - go to different state
        state = smi_power_state::leds_off;
      } else {
        state = smi_power_state::process_power_off_conditions;
      }
      break;

    case smi_power_state::process_power_off_conditions:
      power_timeout = 25;
      power_timeout2 = 3;

      if (flags.power_change_requested || flags.av_power_button_event) {
        state = smi_power_state::wait_for_os_power_off;
      } else if (flags.tray_opening_event) {
        // Tray event resets the Xbox unless reset-on-eject is disabled
        if (flags.reset_on_eject_disabled) {
          state = smi_power_state::event_interrupt_handled;
        } else {
          state = smi_power_state::wait_for_os_response;
        }
      } else if (flags.tray_media_event || flags.tray_state_00_event || flags.av_unplugged_event || flags.video_mode_changed) {
        // Plain notifications, nothing else to do once the SMI has been sent
        state = smi_power_state::event_interrupt_handled;
      } else {
        state = smi_power_state::wait_for_os_response;
      }
      break;

    case smi_power_state::wait_for_os_power_off:
      // Power off: let the OS shut down first. Without a passed RAM test or
      // SMBus 0x1A (os_ready) there is no OS to ask, so power off right away.
      if (!flags.power_off_request) {
        if (!BootChallenge::flags.ram_test_submitted) {
          state = smi_power_state::request_tray_close;
        } else if (!BootChallenge::flags.ram_test_passed) {
          state = smi_power_state::request_tray_close;
        } else if (!flags.os_ready) {
          state = smi_power_state::request_tray_close;
        } else if (flags.os_busy) {
          state = smi_power_state::os_busy_limit;
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
      // Use the pending copy, the host may already have read and cleared interrupt_reason
      if (flags.pending_interrupt_reason & InterruptReason_av_mode_changed) {
        flags.video_mode_changed = false;
      }
      if (flags.pending_interrupt_reason & InterruptReason_av_unplugged) {
        flags.av_unplugged_event = false;
      }
      if (flags.pending_interrupt_reason & InterruptReason_dvd_tray0) {
        flags.tray_media_event = false;
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

    case smi_power_state::os_busy_wait:
      // The OS answered "busy" (SMBus 0x0D bit 1): give it up to 255 more ticks
      power_timeout = 0xFF;
      flags.os_busy = false;
      flags.waiting_for_os = true;
      state = smi_power_state::wait_for_os_power_off;
      break;

    case smi_power_state::request_tray_close:
      flags.prepare_for_shutdown = true;  // System shutting down
      power_timeout = 25;
      state = smi_power_state::wait_tray_close;
      break;

    case smi_power_state::leds_off:
      Led::clearPhases();
      PLL_Reset::flags.cold_reset_request = true;
      PLL_Reset::flags.run_after_reset = false;
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
      BootChallenge::flags.frag_power_off = false;
      flags.system_running = false;
      flags.video_mode_changed = false;
      flags.av_unplugged_event = false;
      flags.tray_media_event = false;
      flags.tray_opening_event = false;
      flags.power_change_requested = false;
      flags.av_power_button_event = false;

      if (flags.power_cycle_request) {
        flags.power_cycle_request = false;
        // Power cycle requested (SMBus 0x02 bit 6): power back on after the
        // SMBus 0x22 delay minus 2 ticks, as P11/P2L (P01 waits 50).
        // Like the PIC's DECFSZ, a result of 0 counts 256 ticks
        power_timeout = (uint8_t)(power_cycle_delay - 2);
        if (power_timeout == 0) {
          power_timeout = 256;
        }
        state = smi_power_state::wait_state_for_power_cycle;
      } else if (flags.first_execution) {
        // Just powered up
        state = smi_power_state::decision_state;
        flags.prepare_for_shutdown = false;
      } else {
        return 1;  // Powered off
      }
      break;

    case smi_power_state::overheat_cooldown_wait:
      if (!Fan::flags.overheated) {
        power_timeout = 1;
        state = smi_power_state::delay;
      }
      break;

    case smi_power_state::wait_for_os_response:
      // Power/eject/tray SMI sent: wait for the OS to answer with SMBus 0x0D,
      // reset the Xbox if it doesn't within 25 ticks
      if (flags.os_event_handled) {
        // OS handled it (SMBus 0x0D), unless it's a tray event with
        // reset-on-eject enabled: that one still resets after the timeout
        if (flags.tray_opening_event && !flags.reset_on_eject_disabled) {
          if (--power_timeout == 0) {
            debug::print_message("SMI: Tray opened with reset on eject on (SMBus 0x19), resetting");
            state = smi_power_state::going_to_reset;
          }
        } else {
          state = smi_power_state::event_interrupt_handled;
        }
      } else if (flags.os_busy) {
        state = smi_power_state::os_busy_wait;
      } else if (flags.power_off_request) {
        state = smi_power_state::request_tray_close;
      } else if (flags.os_reset_request) {
        debug::print_message("SMI: OS asked for a reset (SMBus 0x0D bit 3)");
        state = smi_power_state::going_to_reset;
      } else {
        if (--power_timeout == 0) {
          char msg[64];
          snprintf(msg, sizeof(msg), "SMI: No OS answer (SMBus 0x0D) to interrupt 0x%02X, resetting", flags.pending_interrupt_reason);
          debug::print_warn(msg);
          state = smi_power_state::going_to_reset;
        }
      }
      break;

    case smi_power_state::going_to_reset:
      flags.os_reset_request = false;
      PLL_Reset::flags.warm_reset_request = true;
      flags.video_mode_changed = false;
      flags.av_unplugged_event = false;
      flags.tray_media_event = false;
      flags.tray_opening_event = false;
      state = smi_power_state::decision_state;
      break;

    case smi_power_state::os_busy_limit:
      // Allow "busy" at most 3 times (power_timeout2), then power off anyway
      if (--power_timeout2 != 0) {
        state = smi_power_state::os_busy_wait;
      } else {
        state = smi_power_state::request_tray_close;
      }
      break;

    case smi_power_state::delay:
      // Proceed at once if the PLL is already off, otherwise after the timeout
      if (!PLL_Reset::isHeldInReset() && --power_timeout != 0) {
        break;
      }

      // Stop watching POWOK before dropping POWON, so the expected power loss
      // doesn't trigger the power-failure handler
      setPowerOkIrq(false);
      power_timeout = 2;
      state = smi_power_state::delayed_turning_off;
      break;

    case smi_power_state::wait_for_tray_stable:
      Dvd::sensors.tray_status = Dvd::readTrayState();

      if (Dvd::sensors.tray_status == Dvd::drive_tray_state::closed || Dvd::sensors.tray_status == Dvd::drive_tray_state::no_disc ||
          Dvd::sensors.tray_status == Dvd::drive_tray_state::disc_detected) {
        state = smi_power_state::leds_off;
      } else if (--power_timeout == 0) {
        state = smi_power_state::leds_off;
      }
      break;

    case smi_power_state::delayed_turning_off:
      // Delayed turning power off
      if (power_timeout > 1) {
        power_timeout--;
        break;
      }
      if (Led::isFadingOut()) {
        break;  // SMC_LED_FADE: power off once the LEDs have faded out (0.5 s)
      }
      hal::turn_off_psu();
      state = smi_power_state::initial;
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

  return 0;  // System powered on
}

}  // namespace SMI
}  // namespace SMC
