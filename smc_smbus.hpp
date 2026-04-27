#ifndef __SMC_SMBUS__
#define __SMC_SMBUS__

#include <cstdint>

// pico stuff
#include "hardware/i2c.h"
#include "i2c_slave/include/i2c_slave.h"

namespace SMC {

void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event);
void read_temperatures();

}

#endif // __SMC_SMBUS__
