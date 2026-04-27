#ifndef __SMC__
#define __SMC__

#include <cstdint>
#include <stdio.h>
#include "debug.hpp"
#include "smc_types.hpp"
#include "fsm_led.hpp"
#include "smc_video.hpp"

// pico stuff
#include "hardware/i2c.h"
#include "i2c_slave/include/i2c_slave.h"

namespace SMC {

// TODO: remove hacks
extern challenge_struct challenge;
extern state_struct state;
extern state_struct state_previous;
extern flags_struct flags;
extern sensors_struct sensors;
extern timers_struct timers;
extern config_struct config;
extern bool something_with_cpu_temp;
extern volatile uint8_t ram_test_response0;
extern volatile uint8_t ram_test_response1;
extern volatile uint8_t bios_response_byte0;
extern volatile uint8_t bios_response_byte1;
extern FSM_Leds fsm_leds;

void configureConexantEncoder();

void fireSystemInterrupt();

void resetInterruptReason();
void setInterruptReason(uint8_t ir);
void clearInterruptReason(uint8_t ir);
bool checkInterruptReason(uint8_t ir);
void setStatusBit(uint8_t status_bit);
void clearStatusBit(uint8_t status_bit);
uint8_t checkStatusBit(uint8_t status_bit);

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
