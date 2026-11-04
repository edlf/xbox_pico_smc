#include "pico/stdlib.h"

#include "hal.hpp"
#include "pin_assignments.hpp"
#include "debug.hpp"

namespace hal {

// SMBus master on the slave's own pins, bit-banged like the PIC does.
// Switching the I2C block between slave and master doesn't
// work reliably once the host has used the slave, so the block stays a slave
// and only loses its pins for the duration of a transfer.
namespace {

constexpr uint32_t i2c_half_period_us = 5;  // ~100 kHz
constexpr uint32_t i2c_clock_stretch_timeout_us = 1000;
constexpr uint32_t i2c_bus_idle_us = 50;

// Open drain by hand: output low to pull down, input to let the pull-up win
void sclLow() {
  gpio_set_dir(pins::I2C_SCL, GPIO_OUT);
}

void sdaLow() {
  gpio_set_dir(pins::I2C_SDA, GPIO_OUT);
}

void sdaRelease() {
  gpio_set_dir(pins::I2C_SDA, GPIO_IN);
}

void halfPeriod() {
  busy_wait_us(i2c_half_period_us);
}

bool sclReleaseAndWait() {
  gpio_set_dir(pins::I2C_SCL, GPIO_IN);

  const absolute_time_t deadline = make_timeout_time_us(i2c_clock_stretch_timeout_us);

  while (!gpio_get(pins::I2C_SCL)) {
    if (time_reached(deadline)) {
      return false;
    }
  }

  return true;
}

void takePins() {
  gpio_put(pins::I2C_SCL, 0);
  gpio_put(pins::I2C_SDA, 0);
  gpio_set_dir(pins::I2C_SCL, GPIO_IN);
  gpio_set_dir(pins::I2C_SDA, GPIO_IN);
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_SIO);
  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_SIO);
}

void returnPins() {
  gpio_set_function(pins::I2C_SCL, GPIO_FUNC_I2C);
  gpio_set_function(pins::I2C_SDA, GPIO_FUNC_I2C);
}

// Both lines must stay high for a while, so we don't cut into someone else's
// transfer (the input works whatever the pin function is)
bool busIdle() {
  const absolute_time_t deadline = make_timeout_time_us(i2c_bus_idle_us);

  while (!time_reached(deadline)) {
    if (!gpio_get(pins::I2C_SCL) || !gpio_get(pins::I2C_SDA)) {
      return false;
    }
  }

  return true;
}

I2CMasterResult start(bool repeated) {
  sdaRelease();
  halfPeriod();

  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }

  if (!gpio_get(pins::I2C_SDA)) {
    return repeated ? I2CMasterResult::arbitration_lost : I2CMasterResult::bus_busy;
  }

  halfPeriod();
  sdaLow();

  halfPeriod();
  sclLow();

  return I2CMasterResult::ok;
}

void stop() {
  sdaLow();
  halfPeriod();

  sclReleaseAndWait();
  halfPeriod();

  sdaRelease();
  halfPeriod();
}

I2CMasterResult writeBit(bool bit) {
  if (bit) {
    sdaRelease();
  } else {
    sdaLow();
  }

  halfPeriod();

  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }

  // Someone else pulling SDA low while we send a 1: they win the bus
  const bool lost = bit && !gpio_get(pins::I2C_SDA);

  halfPeriod();
  sclLow();

  return lost ? I2CMasterResult::arbitration_lost : I2CMasterResult::ok;
}

I2CMasterResult readBit(bool& bit) {
  sdaRelease();
  halfPeriod();

  if (!sclReleaseAndWait()) {
    return I2CMasterResult::timeout;
  }

  bit = gpio_get(pins::I2C_SDA);

  halfPeriod();
  sclLow();

  return I2CMasterResult::ok;
}

I2CMasterResult writeByte(uint8_t value) {
  for (int8_t i = 7; i >= 0; i--) {
    const I2CMasterResult result = writeBit(value & (1 << i));
    if (result != I2CMasterResult::ok) {
      return result;
    }
  }

  bool                  nack;
  const I2CMasterResult result = readBit(nack);

  if (result != I2CMasterResult::ok) {
    return result;
  }

  return nack ? I2CMasterResult::nack : I2CMasterResult::ok;
}

I2CMasterResult readByte(uint8_t& value, bool ack) {
  value = 0;

  for (uint8_t i = 0; i < 8; i++) {
    bool                  bit;
    const I2CMasterResult result = readBit(bit);

    if (result != I2CMasterResult::ok) {
      return result;
    }

    value = (value << 1) | bit;
  }

  return writeBit(!ack);
}

I2CMasterResult transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  I2CMasterResult result = start(false);

  if (result == I2CMasterResult::ok) {
    result = writeByte(address << 1);
  }

  for (size_t i = 0; i < tx_len && result == I2CMasterResult::ok; i++) {
    result = writeByte(tx[i]);
  }

  if (result == I2CMasterResult::ok && rx_len > 0) {
    result = start(true);

    if (result == I2CMasterResult::ok) {
      result = writeByte((address << 1) | 1);
    }

    for (size_t i = 0; i < rx_len && result == I2CMasterResult::ok; i++) {
      result = readByte(rx[i], i + 1 < rx_len);
    }
  }

  if (result == I2CMasterResult::arbitration_lost || result == I2CMasterResult::bus_busy) {
    // Not our bus right now, just let go
    sdaRelease();
    sclReleaseAndWait();
  } else {
    stop();
  }

  return result;
}

}  // namespace

I2CMasterResult i2c_master_transfer(uint8_t address, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len) {
  if (!busIdle()) {
    return I2CMasterResult::bus_busy;
  }

  takePins();
  const I2CMasterResult result = transfer(address, tx, tx_len, rx, rx_len);
  returnPins();

  return result;
}

// Frees a device stuck holding SDA low: 9 clocks on SCL, then a STOP. The PIC
// does the same when the temperature sensor stops answering.
void i2c_bus_recovery() {
  debug::print_message("I2C: Bus recovery");

  takePins();

  for (uint8_t i = 0; i < 9; i++) {
    sclLow();
    halfPeriod();
    sclReleaseAndWait();
    halfPeriod();
  }

  stop();
  returnPins();
}

}  // namespace hal
