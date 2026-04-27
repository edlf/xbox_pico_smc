
#ifndef __SMC_PLL_RESET__
#define __SMC_PLL_RESET__

namespace SMC {
namespace PLL_Reset {

enum class pll_sysreset_state {
  initial,
  state1,
  state2,
  cold_reset,
  warm_reset_2,
  warm_reset_1,
  state6,
  state7,
  state8
};

void init();
bool isState1();
void setWarmReset1();
void setColdReset();
void update();

} // namespace BootChallenge
} // namespace PLL_Reset

#endif // __SMC_PLL_RESET__
