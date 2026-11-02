#ifndef SMC_ENCODER_MODE_HPP
#define SMC_ENCODER_MODE_HPP

#include <cstdint>

namespace SMC {
namespace EncoderMode {

// Works out the video mode from the CPU's writes to the video encoder (and
// the X-HD mode packet, when an X-HD HDMI board is fitted), as
// seen by the I2C sniffer, and logs it (INFO) when it changes. Decoding ported
// from X-HD Redux (src/application/smbus_probe.c):
// - Xcalibur (1.6): the full mode, from its raster registers
// - Conexant (1.0-1.3) and Focus (1.4-1.5): interlaced or progressive, the
//   active width, 4:3 or 16:9 and the line standard (from the WSS data), and
//   RGB or YCrCb input. These encoders don't hold the whole mode.

// Power on: forget everything decoded
void reset();
// One data byte the CPU wrote: encoder address, register byte of the write,
// index of the data byte after it, and its value
void onWrite(uint8_t address, uint8_t reg, uint8_t index, uint8_t value);
// Called once per tick: logs the mode once a mode set has settled
void update();

} // namespace EncoderMode
} // namespace SMC

#endif // SMC_ENCODER_MODE_HPP
