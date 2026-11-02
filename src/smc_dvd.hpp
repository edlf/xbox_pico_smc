#ifndef SMC_DVD_HPP
#define SMC_DVD_HPP

#include <cstdint>

namespace SMC {
namespace Dvd {

// Tray state reported by the DVD drive on its three state pins (PORTB & 0x70 on
// the PIC), as returned by SMBus 0x03 (without the "moving" bit 0)
enum class drive_tray_state : uint8_t {
  closed = 0x00,
  open = 0x10,
  unloading = 0x20,
  opening = 0x30,
  no_disc = 0x40,  // Closed, no disc
  closing = 0x50,
  disc_detected = 0x60,  // Closed, disc found
  drive_off = 0x70       // Or no drive
};

struct sensors_struct {
  drive_tray_state tray_status;  // Debounced tray state, SMBus 0x03
};
extern sensors_struct sensors;

// Tray state debounce
enum class dvd_tray_state {
  initial,        // Start the start-up delay
  startup_delay,  // Ignore the tray for 32 ticks (640 ms) after power on
  watch,          // Wait for the pins to differ from the reported state
  confirm,        // Read again, back to watch unless it matches the first read
  apply           // Report the new state and raise its SMI
};

enum class update_eject_tray_state { initial, tick_timer, release_eject };

enum class update_dvd_tray_three_state { initial, wait, eject };

// Tray flags
struct flags_struct {
  bool eject_change_requested;  // Eject button
  bool tray_eject_request;      // SMBus 0x0C = 0
  bool tray_load_request;       // SMBus 0x0C = 1
  bool tray_moving;             // Reported in SMBus 0x03 bit 0
  bool eject_pulse;             // Pulse the drive's eject line
  bool tray_events_enabled;     // Tray changes raise SMIs (after the first unknown state)
  bool eject_button_event;      // Eject button pressed, tracked until the tray moves
  bool tray_open_expected;      // Eject from a closed tray, a closing state doesn't end the move
};
extern flags_struct flags;

void init();
void initDvdTray();
void updateDvdTrayEject();
void updateEjectTray();
void updateDvdTrayThree();
void updateDvdTray();
bool isTrayClosing();

// Warm reset: restart the tray debounce, ignoring tray changes for ~5 s
void restartTrayDebounce();

// Boot challenge start: restart the tray debounce and tray moving state machines
void             restartTrayTracking();
drive_tray_state readTrayState();  // Tray state pins, PORTB & 0x70 on the PIC

}  // namespace Dvd
}  // namespace SMC

#endif  // SMC_DVD_HPP
