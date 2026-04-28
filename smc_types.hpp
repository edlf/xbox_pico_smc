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

enum class Encoder {
  Conexant,
  Focus,
  Xcalibur,
  NoEncoder
};

} // namespace SMC

#endif // __SMC_TYPES__
