#ifndef SMC_I2C_SNIFFER_HPP
#define SMC_I2C_SNIFFER_HPP

#include <cstdint>

namespace SMC {
namespace I2CSniffer {

// Listens to the whole SMBus (not just our address 0x10) for the video
// encoder (Conexant 0x45, Focus 0x6A, Xcalibur 0x70): logs which one answers
// the CPU after power on, and at DEBUG level every write the CPU makes to it.
// Nothing in the SMC acts on them. SMC_I2C_SNIFFER, see CMakeLists.txt.

void init();
// Called once per tick: decodes what was captured and prints what was found
void update();
// Power on/off: log the encoder found again on the next boot
void resetFound();
// Around our own master transfers (hal::i2c_master_transfer), so the SMC's own
// encoder writes aren't logged as the CPU's
void beginOwnTransfer();
void endOwnTransfer();

}  // namespace I2CSniffer
}  // namespace SMC

#endif  // SMC_I2C_SNIFFER_HPP
