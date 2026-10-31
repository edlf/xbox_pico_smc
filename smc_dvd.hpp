#ifndef __SMC_DVD__
#define __SMC_DVD__

#include <cstdint>

// Set by CMake (option SMC_DVD_SUPPORT). Without it there are no tray state
// machines, and the tray is reported as closed and empty.
#ifndef SMC_DVD_SUPPORT
#define SMC_DVD_SUPPORT 1
#endif

namespace SMC {
namespace Dvd {

// Tray state debounce (0x9D3)
enum class dvd_tray_state {
  initial,       // Start the start-up delay
  startup_delay, // Ignore the tray for 32 ticks (640 ms) after power on
  watch,         // Wait for the pins to differ from the reported state
  confirm,       // Read again, back to watch unless it matches the first read
  apply          // Report the new state and raise its SMI
};

enum class update_eject_tray_state {
  initial,
  tick_timer,
  release_eject
};

enum class update_dvd_tray_three_state {
  initial,
  wait,
  eject
};

void init();
void initDdvdTray();
void updateDvdTrayEject();
void updateEjectTray();
void updateDvdTrayThree();
void updateDvdTray();
bool isTrayClosing();
// P11: restart the tray debounce, ignoring tray changes for ~5 s (warm reset)
void restartTrayDebounce();
uint8_t readTrayState(); // Tray state pins, PORTB & 0x70 on the PIC

} // namespace DVD
} // namespace SMC

#endif // __SMC_DVD__
