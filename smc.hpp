#ifndef __SMC__
#define __SMC__

#include <cstdint>
#include <stdio.h>
#include "debug.hpp"
#include "smc_types.hpp"
#include "smc_led.hpp"
#include "smc_video.hpp"
#include "smc_SMI.hpp"

namespace SMC {

// TODO: remove hacks
extern state_struct state;
extern state_struct state_previous;
extern flags_struct flags;
extern sensors_struct sensors;
extern timers_struct timers;
extern bool something_with_cpu_temp;
extern volatile uint8_t ram_test_response0;
extern volatile uint8_t ram_test_response1;
extern volatile uint8_t bios_response_byte0;
extern volatile uint8_t bios_response_byte1;

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
void smc_gpio_callback(unsigned int gpio, uint32_t events);

uint8_t update_power_standby();

void update_PLL_SYSRESET();

void update_LEDs();

}  // namespace SMC

#endif  // __SMC__
