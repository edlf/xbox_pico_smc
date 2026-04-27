#ifndef __DEBUG__
#define __DEBUG__

#include <cstdint>
#include <stdio.h>
#include <string>
#include "smc_types.hpp"

namespace debug {

enum class DebugLevel {
    All,
    Critical,
    Errors,
    Warnings,
    Silent
};

void print_critical(const std::string&);
void print_error(const std::string&);
void print_warn(const std::string&);
void print_message(const std::string&);

void print_welcome();

// SMC state related debug
void print_states(const SMC::state_struct& status);

} // namespace debug

#endif // __DEBUG__
