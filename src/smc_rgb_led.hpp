#ifndef SMC_RGB_LED_HPP
#define SMC_RGB_LED_HPP

#include <cstdint>

namespace SMC {
namespace RgbLed {

// WS2812 RGB LED on GP27 (SMC_RGB_LED) showing how far the Xbox got in
// booting: a colour per boot phase, green for 5 s once it's done, then dark.
enum class rgb_phase {
  off,         // Xbox off: dark
  power_on,    // Powered, held in reset: magenta
  bios,        // Out of reset, waiting for the BIOS RAM test: blue
  kernel,      // RAM test passed, waiting for the challenge answer or OS ready: cyan
  done,        // Booted: green for 5 s
  idle,        // Dark until the next reset or power off
  failed       // RAM test failed (the failed bank's colour) or boot challenge gave up: red
};

void init();
// Called once per tick, in standby and while running
void update();

} // namespace RgbLed
} // namespace SMC

#endif // SMC_RGB_LED_HPP
