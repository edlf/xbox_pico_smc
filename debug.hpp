#ifndef __DEBUG__
#define __DEBUG__

#include <cstdint>
#include <stdio.h>
#include <string>
#include "smc_types.hpp"

namespace debug {

// Ordered by verbosity: a level prints its own messages and everything above it
enum class DebugLevel {
    Silent,
    Critical,
    Errors,
    Warnings,
    All
};

void print_critical(const std::string&);
void print_error(const std::string&);
void print_warn(const std::string&);
void print_message(const std::string&);
void print_welcome();

} // namespace debug

#endif // __DEBUG__
