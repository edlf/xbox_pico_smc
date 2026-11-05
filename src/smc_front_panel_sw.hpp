#ifndef SMC_FRONT_PANEL_SW_HPP
#define SMC_FRONT_PANEL_SW_HPP

namespace SMC {
namespace FrontPanelSW {

// Saved settings, all on/off. Without the power button the Pico's BOOTSEL
// button is the power button: it can't be off then, whatever its setting.
// Without the eject button the tray only moves on the OS's request (SMBus).
bool powerButtonEnabled();
void setPowerButtonEnabled(bool on);
bool ejectButtonEnabled();
void setEjectButtonEnabled(bool on);
bool bootselPowerSetting();  // As set
bool bootselIsPower();       // In use: set, or the power button is off
void setBootselPower(bool on);

// A press of the power / eject button from elsewhere (UART console, web
// interface), counted as a front panel one: only while the other button is
// idle (as P2L). False, and ignored, while it's held.
bool pressPower();
bool pressEject();

void init();
void update();
void updatePower();
void updateEject();
void printPowerState();
void printEjectState();

}  // namespace FrontPanelSW
}  // namespace SMC

#endif  // SMC_FRONT_PANEL_SW_HPP
