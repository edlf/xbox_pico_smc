#ifndef __SMC__
#define __SMC__

#include <cstdint>
#include <stdio.h>
#include "debug.hpp"
#include "smc_types.hpp"

// pico stuff
#include "hardware/i2c.h"
#include "i2c_slave/include/i2c_slave.h"

namespace SMC {
void main_loop();
void globals_init();
void port_init();

void init_irqs();
void isr();
void gpio_callback(uint gpio, uint32_t events);

uint8_t update_power_standby();

void update_dvd_tray_eject();
void update_eject_tray();
void update_dvd_tray3();
void update_dvd_tray();

void update_PLL_SYSRESET();

uint8_t update_SMI_and_power();

void update_pwr_sw();
void update_eject_sw();

void update_video_mode();

void update_LEDs();

void update_audio_clamp();

void update_fan_temp();
void set_fan_speed();

void update_boot_challenge();
void boot_challenge_compute();

// SMBus / I2C
void handle_SMBus_interrupt(i2c_inst_t* i2c, i2c_slave_event_t event);

void read_temperatures();

}  // namespace SMC

#endif  // __SMC__
