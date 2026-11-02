#ifndef SMC_FRONT_PANEL_SW_HPP
#define SMC_FRONT_PANEL_SW_HPP

namespace SMC {
namespace FrontPanelSW {

void init();
void update();
void updatePower();
void updateEject();
void printPowerState();
void printEjectState();

}  // namespace FrontPanelSW
}  // namespace SMC

#endif  // SMC_FRONT_PANEL_SW_HPP
