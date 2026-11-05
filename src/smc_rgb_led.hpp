#ifndef SMC_RGB_LED_HPP
#define SMC_RGB_LED_HPP

#include <cstdint>

namespace SMC {
namespace RgbLed {

// WS2812 RGB LED on GP27 (SMC_RGB_LED) showing how far the Xbox got in
// booting: a colour per boot phase, green for 5 s once it's done, then dark
// (or, in mirror mode, what the front panel LEDs show).
enum class rgb_phase {
  off,       // Xbox off: dark
  power_on,  // Powered, held in reset: magenta
  bios,      // Out of reset, waiting for the BIOS RAM test: blue
  kernel,    // RAM test passed, waiting for the challenge answer or OS ready: cyan
  done,      // Booted: green for 5 s
  idle,      // After that: dark, or the front panel in mirror mode
  failed     // RAM test failed (the failed bank's colour) or boot challenge gave up: red
};

// What the LED is used for (saved in the settings, SMC_RGB_MODE by default)
enum class Mode : uint8_t {
  off,    // Always dark, even while booting
  boot,   // The boot phases, then dark
  mirror  // The boot phases, then the front panel LEDs (green, red or orange)
};
constexpr uint8_t mode_count = 3;

const char* modeName(Mode mode);  // "off", "boot", "mirror"
bool        parseMode(const char* name, Mode& mode);
Mode        mode();
void        setMode(Mode mode);

// Brightness in percent, 1-100 (saved, SMC_RGB_BRIGHTNESS by default)
uint8_t brightness();
bool    setBrightness(uint8_t percent);  // false when out of range

// A colour set over SMBus (0x38-0x3A), shown instead of the automatic ones
// while 0x37 bit 0 is set. A reset or power off goes back to automatic.
void    showSmbusColour(bool show);
bool    smbusColourShown();
void    setSmbusColour(uint8_t channel, uint8_t value);  // 0 red, 1 green, 2 blue
uint8_t smbusColour(uint8_t channel);

void init();
// Called once per tick, in standby and while running
void update();

}  // namespace RgbLed
}  // namespace SMC

#endif  // SMC_RGB_LED_HPP
