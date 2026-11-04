#ifndef SMC_I2C_DECODER_HPP
#define SMC_I2C_DECODER_HPP

#include <cstdint>

namespace SMC {
namespace I2CDecoder {

// Decodes the SMBus from its SDA and SCL levels, as the sniffer
// (smc_i2c_sniffer.cpp) captures them: notes which video encoder answers the
// CPU, hands the CPU's encoder writes to EncoderMode, and at DEBUG level logs them.

// The bus after a change: bit 0 SDA, bit 1 SCL. Repeats are ignored.
void sample(uint8_t state);

// Called once per tick: prints the encoder found and the writes seen
void report();

// Power on/off: log the encoder found again on the next boot
void resetFound();

// During our own master transfers: their bytes aren't the CPU's
void setIgnoring(bool ignore);

}  // namespace I2CDecoder
}  // namespace SMC

#endif  // SMC_I2C_DECODER_HPP
