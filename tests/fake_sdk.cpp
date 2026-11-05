// Fake Pico SDK time and GPIO IRQs, and stubs for the modules that only run on
// the Pico (the sniffer's PIO/DMA)

#include <cstdio>

#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "fakes.hpp"
#include "config.hpp"
#include "smc_i2c_sniffer.hpp"

i2c_inst_t fake_i2c0;

void pico_get_unique_board_id_string(char* id_out, unsigned len) {
  snprintf(id_out, len, "%s", fake::board_id);
}

static uint64_t time_now_us = 0;

namespace fake {

void (*tick_hook)() = nullptr;
Watchdog    watchdog;
GpioIrq     power_ok_irq;
bool        echo_log = false;
bool        echo_debug = false;
const char* board_id = "E6614103E7123456";

uint64_t now_us() {
  return time_now_us;
}

void advance_us(uint64_t us) {
  time_now_us += us;
}

void checkWatchdog() {
  if (watchdog.enabled && time_now_us - watchdog.last_pet_us > 1000000) {
    watchdog.enabled = false;  // Off again after the reboot, until main_loop() enables it
    throw WatchdogReboot{};
  }
}

}  // namespace fake

uint32_t time_us_32() {
  return static_cast<uint32_t>(time_now_us);
}
uint64_t time_us_64() {
  return time_now_us;
}
void busy_wait_us(uint32_t us) {
  time_now_us += us;
  fake::checkWatchdog();
}
void busy_wait_ms(uint32_t ms) {
  time_now_us += uint64_t{ms} * 1000;
  fake::checkWatchdog();
}

void gpio_acknowledge_irq(uint, uint32_t) {}

// Only the POWER_OK interrupt is used
void gpio_set_irq_enabled_with_callback(uint, uint32_t, bool enabled, gpio_irq_callback_t callback) {
  fake::power_ok_irq.enabled = enabled;
  fake::power_ok_irq.callback = callback;
}

namespace SMC {

namespace I2CSniffer {
static bool enable = SMC_I2C_SNIFFER;
bool        enabled() {
  return enable;
}
void setEnabled(bool on) {
  enable = on;
}
void init() {}
void update() {}
void beginOwnTransfer() {}
void endOwnTransfer() {}
void resetFound() {}
}  // namespace I2CSniffer

}  // namespace SMC
