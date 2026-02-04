#ifndef __SMC_DEBUG__
#define __SMC_DEBUG__

#include <stdint.h>
#include <stdio.h>
#include "smc_types.hpp"

namespace SMC {
namespace debug {

void print_states(const state_struct& status);
void print_state_changes(const state_struct& status, const state_struct& status_previous);

} // namespace debug
} // namespace SMC

#endif // __SMC_DEBUG__
