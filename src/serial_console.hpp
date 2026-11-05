#ifndef SERIAL_CONSOLE_HPP
#define SERIAL_CONSOLE_HPP

namespace SMC {
namespace SerialConsole {

// Commands on the UART console (GP0 TX / GP1 RX, the one the log goes to)
// while the SMC runs, one per line:
//   power   press the power button (powers on from standby, or the SMI)
//   eject   press the eject button
//   help    the commands
// The same as the front panel buttons (and the web interface's), even with
// those turned off in the settings: one counts only while the other button
// isn't held, see FrontPanelSW::pressPower(). The USB serial port is only there in USB
// config mode, see usb_config.hpp.

// Called once per tick, in standby and while running: takes what came in
void service();

}  // namespace SerialConsole
}  // namespace SMC

#endif  // SERIAL_CONSOLE_HPP
