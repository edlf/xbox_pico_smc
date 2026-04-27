#ifndef __SMC_TYPES__
#define __SMC_TYPES__

#include <cstdint>
#include <stdio.h>

constexpr uint8_t MAX_BOARD_TEMP = 75;
constexpr uint8_t MAX_CPU_TEMP = 88;
constexpr uint8_t temperature_history_length = 16;

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

enum Status : uint8_t {
  // bit 0 is set when power switch is pressed and when power off is requested
  // via SMBus
  power_change_requested = 0b00000001, // 0, 0x01
  // bit 1 is set when eject switch is pressed
  eject_change_requested = 0b00000010, // 1, 0x02
  // bit 2 is set for the first execution of the main loop after the Xbox turns on
  first_execution = 0b00000100,   // 2, 0x04
  audio_clamp_timer = 0b00001000, // 3, 0x08
  // bit 4 is set when the system is overheated
  overheated = 0b00010000, // 4, 0x10
  // bit 5 is set when the SMC should prepare for Xbox shutdown (tray close,
  // turn off audio clamp, don't change fan speed, don't monitor boot challenge)
  prepare_for_shutdown = 0b00100000, // 5, 0x20
  // bit 6 is DVD tray related (causes SMI interrupt to be fired)
  dvd_tray = 0b01000000, // 0x40
  // bit 7 is set when vide mode changes (causes SMI interrupt to be fired)
  video_mode_changed = 0b10000000 // 0x80
};

enum class smi_power_state {
  decision_state,
  start_power_off,
  case2,
  case3,
  event_interrupt_handled,
  case5,
  request_tray_close,
  leds_off,
  wait_tray_close,
  initial,
  overheat_cooldown_wait,
  case11,
  going_to_reset,
  case13,
  delay,
  case15,
  delayed_turning_off,
  wait_state_for_initial
};

enum class power_standby_state {
  initial,
  read_av,
  check_av_power_button,
  turn_on_power_no_av,
  powered_up_wait_power_ok,
  check_eject_button,
  idle,
  turn_on_power_alternative,
  powered_up_wait_power_ok_alternative,
  powered_up_alt,
  powered_up
};

enum class pll_sysreset_state {
  initial,
  state1,
  state2,
  cold_reset,
  warm_reset_2,
  warm_reset_1,
  state6,
  state7,
  state8
};

// State Machines
typedef struct {
  power_standby_state standby_power;
  pll_sysreset_state pll_reset;
  smi_power_state smi_power;
} state_struct;

typedef struct {
  uint8_t interrupt_reason; // 6C
  uint8_t bitfield_DATA_6D;
  uint8_t status; // 6E
  uint8_t bitfield_DATA_6F;
  uint8_t bitfield_DATA_70;
  uint8_t bitfield_DATA_71;
  uint8_t bitfield_DATA_72;
  uint8_t bitfield_DATA_73;
  uint8_t bitfield_DATA_74;
} flags_struct;

typedef struct {
  uint8_t CPU_temperature;
  uint8_t CPU_temperature_previous;
  uint8_t board_temperature;
  uint8_t CPU_temp_predicted;
  uint8_t tray_status;
  uint8_t tray_status_raw;
  uint8_t vmode;
  uint8_t vmode_raw;
} sensors_struct;

typedef struct {
  uint16_t power_timeout;
  uint16_t power_timeout3;
  uint16_t smbus_attempt_counter;
} timers_struct;

} // namespace SMC

#endif // __SMC_TYPES__
