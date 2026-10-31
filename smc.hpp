#ifndef SMC_HPP
#define SMC_HPP

#include <cstdint>
#include <stdio.h>
#include "debug.hpp"
#include "smc_types.hpp"
#include "smc_front_panel_sw.hpp"
#include "smc_dvd.hpp"
#include "smc_led.hpp"
#include "smc_video.hpp"
#include "smc_boot_challenge.hpp"
#include "smc_SMI.hpp"
#include "smc_audio_clamp.hpp"
#include "smc_smbus.hpp"
#include "smc_fan.hpp"
#include "smc_pll_reset.hpp"
#include "smc_power_standby.hpp"

namespace SMC {

// State shared by all modules, like the PIC's RAM: flags (0x6C-0x74), sensor
// readings, and the boot results the BIOS writes over SMBus (0x0E, 0x12, 0x13)
extern flags_struct flags;
extern sensors_struct sensors;
extern volatile uint8_t ram_test_response0;
extern volatile uint8_t ram_test_response1;
extern volatile uint8_t bios_response_byte0;

void configureConexantEncoder();
void configureTemperatureSensor();

void fireSystemInterrupt();

void resetInterruptReason();
void setInterruptReason(uint8_t ir);

void main_loop();
void globals_init();

void smc_gpio_callback(unsigned int gpio, uint32_t events);
void setPowerOkIrq(bool enabled);

void update_LEDs();

void shutdown_xbox();
void panic(const std::string message);
}  // namespace SMC

#endif  // SMC_HPP
