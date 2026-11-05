#ifndef USB_COMMANDS_HPP
#define USB_COMMANDS_HPP

namespace UsbConfig {

// The USB config shell's commands (`help` lists them), apart from the USB
// side (usb_config.cpp), so the host tests can run them.

// What run() does once the reply is out
enum class Action {
  none,
  reboot,      // Restart the Pico
  bootloader,  // Restart into the USB bootloader, for a firmware update
};

// One line from the PC. The reply goes to out() as it is (lines ending in
// "\n"), ending in a line "OK" or "ERR <reason>"; an empty line gets none.
// The line is split up in place.
Action command(char* line, void (*out)(const char* text));

}  // namespace UsbConfig

#endif  // USB_COMMANDS_HPP
