#ifndef USB_CONFIG_HPP
#define USB_CONFIG_HPP

namespace UsbConfig {

// Configuration over USB serial, only with the Pico out of the Xbox: powered
// from USB alone, no Xbox standby power. Checked once at start-up, before the
// SMC touches any pin: a USB host has to enumerate the Pico within a second,
// and the Xbox's standby pull-ups on AV mode 0 and 2 must be absent. Then the
// SMC doesn't start at all, the Pico only runs a line-based command shell
// (tools/og_smc_config.py) until it's reset.

// True to run the shell instead of the SMC. Leaves USB off otherwise.
bool              detect();
[[noreturn]] void run();

}  // namespace UsbConfig

#endif  // USB_CONFIG_HPP
