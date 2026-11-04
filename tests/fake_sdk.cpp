// Fake Pico SDK time and GPIO IRQs, and stubs for the modules that only run on
// the Pico (the sniffer's PIO/DMA, the settings in flash)

#include "config.hpp"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "smc_i2c_sniffer.hpp"
#include "smc_settings.hpp"

#include <cstring>

i2c_inst_t fake_i2c0;

static uint64_t now_us = 0;

uint32_t time_us_32() {
  return static_cast<uint32_t>(now_us);
}
uint64_t time_us_64() {
  return now_us;
}
void busy_wait_us(uint32_t us) {
  now_us += us;
}
void busy_wait_ms(uint32_t ms) {
  now_us += uint64_t{ms} * 1000;
}

void gpio_acknowledge_irq(uint, uint32_t) {}
void gpio_set_irq_enabled_with_callback(uint, uint32_t, bool, gpio_irq_callback_t) {}

namespace SMC {

namespace I2CSniffer {
void init() {}
void update() {}
void beginOwnTransfer() {}
void endOwnTransfer() {}
void resetFound() {}
}  // namespace I2CSniffer

namespace Settings {
static char ssid[max_ssid_length + 1] = SMC_WIFI_SSID;
static char password[max_password_length + 1] = SMC_WIFI_PASSWORD;
static bool web = true;

void load() {}
void requestSave() {}
bool saveNow() {
  return true;
}
void        update() {}
const char* wifiSsid() {
  return ssid;
}
const char* wifiPassword() {
  return password;
}
bool setWifiSsid(const char* s) {
  std::strncpy(ssid, s, max_ssid_length);
  return true;
}
bool setWifiPassword(const char* p) {
  std::strncpy(password, p, max_password_length);
  return true;
}
void resetWifi() {}
bool webEnabled() {
  return web;
}
void setWebEnabled(bool enabled) {
  web = enabled;
}
}  // namespace Settings

}  // namespace SMC
