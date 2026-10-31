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
