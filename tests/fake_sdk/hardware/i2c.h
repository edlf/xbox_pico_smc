#ifndef FAKE_HARDWARE_I2C_H
#define FAKE_HARDWARE_I2C_H

// The SMBus slave's data register, as a byte the tests set (what the Xbox
// writes) and read back (what the SMC answers)
#include <cstdint>

struct i2c_inst_t {
  uint8_t rx = 0;  // Next byte from the master
  uint8_t tx = 0;  // Last byte the slave wrote
};
extern i2c_inst_t fake_i2c0;
#define i2c0 (&fake_i2c0)

inline uint8_t i2c_read_byte_raw(i2c_inst_t* i2c) {
  return i2c->rx;
}
inline void i2c_write_byte_raw(i2c_inst_t* i2c, uint8_t value) {
  i2c->tx = value;
}

#endif
