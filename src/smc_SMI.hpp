#ifndef SMC_SMI_HPP
#define SMC_SMI_HPP

#include <cstdint>

namespace SMC {
namespace SMI {

// SMI reasons, the bits of SMBus 0x11 (flags.interrupt_reason)
enum InterruptReason : uint8_t {
  // bit 0 - power button pressed
  InterruptReason_power_sw_pressed = 0b00000001,  // 0x01
                                                  // bit 1 - DVD tray related
  InterruptReason_dvd_tray0 = 0b00000010,         // 0x02
                                                  // bit 2 - DVD tray related
  InterruptReason_dvd_tray1 = 0b00000100,         // 0x04
                                                  // bit 3 - AV mode changed
  InterruptReason_av_mode_changed = 0b00001000,   // 0x08
                                                  // bit 4 - AV cable unplugged
  InterruptReason_av_unplugged = 0b00010000,      // 0x10
                                                  // bit 5 - eject button pressed
  InterruptReason_eject_sw_pressed = 0b00100000,  // 0x20
                                                  // bit 6 - DVD tray related
  InterruptReason_dvd_tray2 = 0b01000000,         // 0x40
                                                  // bit 7 - unused
  InterruptReason_unused = 0b10000000             // 0x80
};

enum class smi_power_state {
  decision_state,
  start_power_off,
  process_power_off_conditions,
  wait_for_os_power_off,
  event_interrupt_handled,
  os_busy_wait,
  request_tray_close,
  leds_off,
  wait_tray_close,
  initial,
  overheat_cooldown_wait,
  wait_for_os_response,
  going_to_reset,
  os_busy_limit,
  delay,
  wait_for_tray_stable,
  delayed_turning_off,
  wait_state_for_power_cycle
};

// SMI and power off flags, from the PIC's RAM (address.bit given for each).
// Set by SMBus, the buttons and the tray/AV state machines, carried out here.
struct flags_struct {
  uint8_t interrupt_reason;  // 6C, InterruptReason bits, read (and cleared) by SMBus 0x11
  // 6D: copy of the reasons of the SMI being handled, so they can be cleared
  // once the OS answers even if it already read and cleared 6C
  uint8_t pending_interrupt_reason;

  bool power_change_requested;  // 6E.0 Power button, or power off/cycle requested over SMBus
  bool first_execution;         // 6E.2 First pass of the main loop after power on
  bool prepare_for_shutdown;    // 6E.5 Close the tray, clamp the audio, hold the fan and boot challenge
  bool system_running;          // 70.3 Powered and running, LEDs off when clear

  // SMI events, raised by the tray and AV state machines
  bool tray_media_event;       // 6E.6 Tray went to 0x40/0x60 (InterruptReason_dvd_tray0)
  bool video_mode_changed;     // 6E.7 InterruptReason_av_mode_changed
  bool tray_opening_event;     // 71.2 Tray went to 0x30 (InterruptReason_dvd_tray1)
  bool av_unplugged_event;     // 71.7 InterruptReason_av_unplugged
  bool av_power_button_event;  // 72.0 Special AV mode, SMI as if the power button was pressed
  bool tray_state_00_event;    // 73.5 Tray went to 0x00 (InterruptReason_dvd_tray2)

  // The OS's answers and requests over SMBus
  bool power_off_request;        // 6F.1 SMBus 0x02 bit 7 or 0x0D bit 0: power off instead of resetting
  bool waiting_for_os;           // 6F.6 Power/eject SMI sent, SMBus 0x0D is accepted
  bool os_event_handled;         // 70.4 SMBus 0x0D bit 2: the OS handled the SMI, no reset
  bool os_busy;                  // 70.5 SMBus 0x0D bit 1: the OS needs more time
  bool os_reset_request;         // 71.3 SMBus 0x02 bit 0 or 0x0D bit 3
  bool reset_on_eject_disabled;  // 72.1 SMBus 0x19 bit 0
  bool reset_on_eject_writable;  // 72.2 SMBus 0x19 can still disable it (once per boot)
  bool os_ready;                 // 72.6 SMBus 0x1A: power off goes through the OS
  bool power_cycle_request;      // 73.6 SMBus 0x02 bit 6
};
extern flags_struct flags;

void init();
// Clear the SMBus 0x11 reasons (flags.interrupt_reason)
void resetInterruptReason();
// Raise an SMI for these reasons (InterruptReason bits): latch them for
// SMBus 0x11 and the pending copy, then pulse the SMI line
void raiseInterrupt(uint8_t reason);
// True once after a power cycle (SMBus 0x02 bit 6), the main loop then
// presses the power button for the host
bool takePowerOnRequest();
// Same, without clearing it
bool powerOnRequested();
// Ticks off during a power cycle (SMBus 0x22), back to 25 in init()
void    setPowerCycleDelay(uint8_t ticks);
void    printState();
uint8_t update();

}  // namespace SMI
}  // namespace SMC

#endif  // SMC_SMI_HPP
