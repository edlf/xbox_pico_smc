#ifndef FAKE_PICO_I2C_SLAVE_H
#define FAKE_PICO_I2C_SLAVE_H
#include "hardware/i2c.h"

typedef enum { I2C_SLAVE_RECEIVE, I2C_SLAVE_REQUEST, I2C_SLAVE_FINISH } i2c_slave_event_t;
typedef void (*i2c_slave_handler_t)(i2c_inst_t* i2c, i2c_slave_event_t event);
inline void i2c_slave_init(i2c_inst_t*, uint8_t, i2c_slave_handler_t) {}

#endif
