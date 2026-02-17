#ifndef __SMC_TYPES__
#define __SMC_TYPES__

#include <stdint.h>
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

enum class eject_switch_state {
  wait_for_button_press,
  debouncing,
  wait_for_button_release,
  release_debounce
};

enum class power_switch_state {
  wait_for_button_press,
  debouncing,
  wait_for_button_release,
  release_debounce
};

enum class led_state {
  initial,
  overheat,
  manual_control,
  quick_green_blink,
  solid_green,
  off,
  tick_update,
  set_gpios,
  reset_phase_counter,
  quick_green_orange
};

enum class audio_state {
  clamped,
  tick_timer,
  unclamped
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

enum class fan_control_state {
  initial,
  state1,
  decision_state,
  overheated,
  overheated_cooldown,
  state5,
  custom_fan_speed,
  board_cool,
  board_hot,
  state9,
  state10,
  state11,
  cpu_hot,
  state13
};

enum class boot_challenge_state {
  initial,
  wait_for_ram_test_result,
  ram_test_ok,
  ram_test_failed,
  challenge_failed_or_no_ram_test_result,
  challenge_wait_for_reply,
  boot_failed,
  lock_up,
  retry_boot,
  check_response,
  challenge_passed,
  reboot
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

enum class dvd_tray_state {
  initial,
  tick_timer,
  state2,
  state3,
  state4
};

enum class update_eject_tray_state {
  initial,
  tick_timer,
  release_eject
};

enum class update_dvd_tray3 {
  initial,
  wait,
  eject
};

// State Machines
typedef struct {
  boot_challenge_state boot_challenge;
  power_switch_state pwr_sw;
  eject_switch_state eject_sw;
  audio_state audio_clamp;
  led_state leds;
  power_standby_state standby_power;
  fan_control_state fan_control;
  pll_sysreset_state pll_reset;

  smi_power_state smi_power;
  uint8_t video_mode;

  dvd_tray_state dvd_tray;
  uint8_t tray_eject;
  update_dvd_tray3 dvd_tray_3; // jump_index_sub_code_519
  update_eject_tray_state update_eject_tray;
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
  uint8_t fan_speed;
  uint8_t tray_status;
  uint8_t tray_status_raw;
  uint8_t vmode;
  uint8_t vmode_raw;
} sensors_struct;

typedef struct {
  uint16_t power_timeout;
  uint16_t power_timeout2;
  uint16_t power_timeout3;
  uint16_t fan_control_timeout1;
  uint16_t fan_control_timeout2;
  uint16_t audio_clamp_timeout;
  uint16_t boot_response_timeout;
  uint16_t smbus_attempt_counter;
  uint16_t tray_state_timer;
  uint16_t eject_timeout;
  uint16_t dvd_tray_timeout;
} timers_struct;

typedef struct {
  uint8_t custom_fan_speed;
  uint8_t LED_red_manual_cycles;
  uint8_t LED_green_manual_cycles;
} config_struct;

typedef struct {
  uint8_t input_byte0; /* Input: Challenge seed uint8_t 0 */
  uint8_t input_byte1; /* Input: Challenge seed uint8_t 1 */
  uint8_t input_byte2; /* Input: Challenge seed uint8_t 2 */
  uint8_t input_byte3; /* Input: Challenge seed uint8_t 3 */
  uint8_t expected0;   /* Output: Expected challenge response uint8_t 0 */
  uint8_t expected1;   /* Output: Expected challenge response uint8_t 1 */
  uint8_t response0;   /* Input: Received challenge response uint8_t 0 */
  uint8_t response1;   /* Input: Received challenge response uint8_t 1 */
  uint8_t status_byte0;
  uint8_t status_byte1;
  uint8_t status_byte2;
  uint8_t status_byte3;
  uint8_t status_byte4;
} challenge_struct;

typedef struct {
  uint8_t state_counter;
  uint8_t green_phases_manual;
  uint8_t red_phases_manual;
  uint8_t green_phases;
  uint8_t red_phases;
} led_struct;

// FAN PWM duty
const uint16_t pwm_duty[] = {
    0x0000,     0x51E * 1,  0x51E * 2,  0x51E * 3,  0x51E * 4,  0x51E * 5,  0x51E * 6,  0x51E * 7,  0x51E * 8,  0x51E * 9,  0x51E * 10, /* 1-10 */
    0x51E * 11, 0x51E * 12, 0x51E * 13, 0x51E * 14, 0x51E * 15, 0x51E * 16, 0x51E * 17, 0x51E * 18, 0x51E * 19, 0x51E * 20,             /* 11-20 */
    0x51E * 21, 0x51E * 22, 0x51E * 23, 0x51E * 24, 0x51E * 25, 0x51E * 26, 0x51E * 27, 0x51E * 28, 0x51E * 29, 0x51E * 30,             /* 21-30 */
    0x51E * 31, 0x51E * 32, 0x51E * 33, 0x51E * 34, 0x51E * 35, 0x51E * 36, 0x51E * 37, 0x51E * 38, 0x51E * 39, 0x51E * 40,             /* 31-40 */
    0x51E * 41, 0x51E * 42, 0x51E * 43, 0x51E * 44, 0x51E * 45, 0x51E * 46, 0x51E * 47, 0x51E * 48, 0x51E * 49, 0xFFFF,                 /* 41-50 */
};

namespace Command {
constexpr uint8_t FIRMWARE_REVISION = 0x01;
constexpr uint8_t RESET = 0x02;
constexpr uint8_t TRAY_STATE = 0x03;
constexpr uint8_t VIDEO_MODE = 0x04;
constexpr uint8_t FAN_OVERRIDE = 0x05;
constexpr uint8_t REQUEST_FAN_SPEED = 0x06;
constexpr uint8_t LED_OVERRIDE = 0x07;
constexpr uint8_t LED_STATES = 0x08;
constexpr uint8_t CPU_TEMPERATURE = 0x09;
constexpr uint8_t AIR_TEMPERATURE = 0x0A;
constexpr uint8_t AUDIO_CLAMP = 0x0B;
constexpr uint8_t DVD_TRAY_OPERATION = 0x0C;
constexpr uint8_t OS_RESUME = 0x0D;
constexpr uint8_t WRITE_ERROR_CODE = 0x0E;
constexpr uint8_t READ_ERROR_CODE = 0x0F;
constexpr uint8_t READ_FAN_SPEED = 0x10;
constexpr uint8_t INTERRUPT_REASON = 0x11;
constexpr uint8_t WRITE_RAM_TEST_RESULTS = 0x12;
constexpr uint8_t WRITE_RAM_TYPE = 0x13;
constexpr uint8_t READ_RAM_TEST_RESULTS = 0x14;
constexpr uint8_t READ_RAM_TYPE = 0x15;
constexpr uint8_t LAST_REGISTER_WRITTEN = 0x16;
constexpr uint8_t LAST_BYTE_WRITTEN = 0x17;
constexpr uint8_t SOFTWARE_INTERRUPT = 0x18;
constexpr uint8_t OVERRIDE_RESET_ON_TRAY_OPEN = 0x19;
constexpr uint8_t OS_READY = 0x1A;
constexpr uint8_t SCRATCH = 0x1B;
}; // namespace Command

} // namespace SMC

#endif // __SMC_TYPES__
