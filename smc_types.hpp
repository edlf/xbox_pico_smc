#ifndef __SMC_TYPES__
#define __SMC_TYPES__

#include <cstdint>
#include <stdio.h>

namespace SMC {

enum InterruptReason : uint8_t {
  // bit 0 - power button pressed
  InterruptReason_power_sw_pressed = 0b00000001, // 0x01
                                                 // bit 1 - DVD tray related
  InterruptReason_dvd_tray0 = 0b00000010,        // 0x02
                                                 // bit 2 - DVD tray related
  InterruptReason_dvd_tray1 = 0b00000100,        // 0x04
                                                 // bit 3 - AV mode changed
  InterruptReason_av_mode_changed = 0b00001000,  // 0x08
                                                 // bit 4 - AV cable unplugged
  InterruptReason_av_unplugged = 0b00010000,     // 0x10
                                                 // bit 5 - eject button pressed
  InterruptReason_eject_sw_pressed = 0b00100000, // 0x20
                                                 // bit 6 - DVD tray related
  InterruptReason_dvd_tray2 = 0b01000000,        // 0x40
                                                 // bit 7 - unused
  InterruptReason_unused = 0b10000000            // 0x80
};

// The PIC keeps these as bits in RAM 0x6C-0x74, the address and bit of each is
// given as 6F.2 etc. for comparing with the disassembly.
typedef struct {
  uint8_t interrupt_reason; // 6C, InterruptReason bits, read (and cleared) by SMBus 0x11
  // 6D: copy of the reasons of the SMI being handled, so they can be cleared
  // once the OS answers even if it already read and cleared 6C
  uint8_t pending_interrupt_reason;

  // Events and states shared by the state machines
  bool power_change_requested; // 6E.0 Power button, or power off/cycle requested over SMBus
  bool eject_change_requested; // 6E.1 Eject button
  bool first_execution;        // 6E.2 First pass of the main loop after power on
  bool audio_clamp_timer;      // 6E.3 Unclamp the audio after a delay (set at power on)
  bool overheated;             // 6E.4
  bool prepare_for_shutdown;   // 6E.5 Close the tray, clamp the audio, hold the fan and boot challenge
  bool tray_media_event;       // 6E.6 Tray went to 0x40/0x60, SMI (InterruptReason_dvd_tray0)
  bool video_mode_changed;     // 6E.7 SMI (InterruptReason_av_mode_changed)

  // Requests from the OS over SMBus, carried out by the state machines
  bool audio_clamp_request;   // 6F.0 SMBus 0x0B = 1
  bool power_off_request;     // 6F.1 SMBus 0x02 bit 7 or 0x0D bit 0: power off instead of resetting
  bool fan_override;          // 6F.2 SMBus 0x05: use the speed from 0x06
  bool tray_eject_request;    // 6F.3 SMBus 0x0C = 0
  bool tray_load_request;     // 6F.4 SMBus 0x0C = 1
  bool tray_moving;           // 6F.5 Reported in SMBus 0x03 bit 0
  bool waiting_for_os;        // 6F.6 Power/eject SMI sent, SMBus 0x0D is accepted
  bool audio_unclamp_request; // 6F.7 SMBus 0x0B = 0, or the AV cable is back

  // System control
  bool cold_reset_request;    // 70.0 Restart the PLL/SYSRESET sequence (shutdown, overheat, boot failure)
  bool run_after_reset;       // 70.1 Come out of reset after a cold reset (clear: stay in reset)
  bool led_override;          // 70.2 SMBus 0x07: show the SMBus 0x08 pattern
  bool system_running;        // 70.3 Powered and running, LEDs off when clear
  bool os_event_handled;      // 70.4 SMBus 0x0D bit 2: the OS handled the SMI, no reset
  bool os_busy;               // 70.5 SMBus 0x0D bit 1: the OS needs more time

  bool tray_opening_event;    // 71.2 Tray went to 0x30, SMI (InterruptReason_dvd_tray1)
  bool os_reset_request;      // 71.3 SMBus 0x02 bit 0 or 0x0D bit 3
  bool ram_test_submitted;    // 71.4 SMBus 0x12 written
  bool ram_test_passed;       // 71.5
  bool av_cable_missing;      // 71.6 LEDs and audio clamp
  bool av_unplugged_event;    // 71.7 SMI (InterruptReason_av_unplugged)

  bool av_power_button_event; // 72.0 Special AV mode, SMI as if the power button was pressed
  bool reset_on_eject_disabled; // 72.1 SMBus 0x19 bit 0
  bool reset_on_eject_writable; // 72.2 SMBus 0x19 can still disable it (once per boot)
  bool eject_pulse;           // 72.3 Pulse the drive's eject line
  bool tray_events_enabled;   // 72.4 Tray changes raise SMIs (after the first unknown state)
  bool frag_power_off;        // 72.5 FRAG: the power button turns off right away
  bool os_ready;              // 72.6 SMBus 0x1A: power off goes through the OS
  bool eject_button_event;    // 72.7 Eject button pressed, tracked until the tray moves

  bool warm_reset_request;    // 73.0 Reset the Xbox (SMBus 0x02 bit 0, or the OS didn't answer)
  bool tray_open_expected;    // 73.1 Eject from a closed tray, a closing state doesn't end the move
  bool frag_eject;            // 73.2 FRAG: the eject button opens the tray directly
  bool system_reset_released; // 73.3 SYSRESET released (never read)
  bool challenge_response_received; // 73.4 SMBus 0x21 written
  bool tray_state_00_event;   // 73.5 Tray went to 0x00, SMI (InterruptReason_dvd_tray2)
  bool power_cycle_request;   // 73.6 SMBus 0x02 bit 6

  bool temperature_read_failed; // 74.0
} flags_struct;

typedef struct {
  uint8_t CPU_temperature;
  uint8_t board_temperature;
  uint8_t tray_status;
  uint8_t vmode;
} sensors_struct;

enum class Encoder {
  Conexant,
  Focus,
  Xcalibur,
  NoEncoder
};

} // namespace SMC

#endif // __SMC_TYPES__
