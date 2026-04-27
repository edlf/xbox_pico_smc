#ifndef __SMC_FRONT_PANEL_SW__
#define __SMC_FRONT_PANEL_SW__

namespace SMC {
namespace FrontPanelSW {

void init();
void update();
void updatePower();
void updateEject();
void printPowerState();
void printEjectState();

} // namespace __SMC_FRONT_PANEL_SW__
} // namespace SMC

#endif // __SMC_LED__
