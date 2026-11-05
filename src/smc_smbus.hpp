#ifndef SMC_SMBUS_HPP
#define SMC_SMBUS_HPP

#include <cstdint>

#include "hardware/i2c.h"
#include "pico/i2c_slave.h"

namespace SMC {

void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event);
bool read_temperatures();
bool smbusMasterWrite(uint8_t address, const uint8_t* data, size_t len);
bool smbusMasterReadByte(uint8_t address, uint8_t reg, uint8_t& value);
// Applies the SMBus writes received since the last call, at the start of each tick
void applySMBusWrites();
void printSMBusLog();
void resetSMBusRegisters();
// The last error code the BIOS or kernel wrote to SMBus 0x0E since the Pico
// started, kept across power off (0x0F itself is cleared then). False if none.
bool lastBiosErrorCode(uint8_t& code);

}  // namespace SMC

#endif  // SMC_SMBUS_HPP
