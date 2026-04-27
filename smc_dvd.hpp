#ifndef __SMC_DVD__
#define __SMC_DVD__

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

} // namespace DVD
} // namespace SMC

#endif // __SMC_DVD__
