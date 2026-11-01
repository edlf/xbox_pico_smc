#ifndef SMC_PLL_RESET_HPP
#define SMC_PLL_RESET_HPP

namespace SMC {
namespace PLL_Reset {

// PLL and SYSRESET sequencing (0x5AF)
enum class pll_sysreset_state {
  initial,                // Assert SYSRESET, re-arm the per-boot flags
  held_in_reset,          // PLL off, wait for run_after_reset and POWOK
  start_delay_unused,     // Never entered: held_in_reset always goes to start_pll
  start_pll,              // Cold start: PLL on, release SYSRESET 2 ticks later
  release_reset,          // Release SYSRESET and configure the encoder (P11: temperature sensor)
  warm_reset,             // SMBus/OS reset: assert SYSRESET, PLL stays on
  restart_pll_off_unused, // Never entered: PLL off and SYSRESET, then restart_pll_on_unused
  restart_pll_on_unused,  // Never entered: PLL on, then release_reset
  running                 // Wait for power loss, cold_reset_request or warm_reset_request
};

// Reset flags, from the PIC's RAM (address.bit)
struct flags_struct {
  bool cold_reset_request;    // 70.0 Restart the PLL/SYSRESET sequence (shutdown, overheat, boot failure)
  bool run_after_reset;       // 70.1 Come out of reset after a cold reset (clear: stay in reset)
  bool warm_reset_request;    // 73.0 Reset the Xbox (SMBus 0x02 bit 0, or the OS didn't answer)
  bool system_reset_released; // 73.3 SYSRESET released (never read)
};
extern flags_struct flags;

void init();
bool isHeldInReset();
void update();
void printState();

} // namespace PLL_Reset
} // namespace SMC

#endif // SMC_PLL_RESET_HPP
