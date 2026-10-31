#include "debug.hpp"
#include <iostream>
#include <iomanip>
#include "pico/time.h"

namespace debug {

// Set by CMake (SMC_DEBUG_LEVEL), the index into DebugLevel
#ifndef SMC_DEBUG_LEVEL
#define SMC_DEBUG_LEVEL 3 // Info
#endif
DebugLevel debugLevel = static_cast<DebugLevel>(SMC_DEBUG_LEVEL);

void print_timestamp() {
    absolute_time_t time = get_absolute_time();
    std::ios_base::fmtflags flags(std::cout.flags());
    std::cout << "[";
    std::cout << std::setfill('0') << std::setw(12);
    std::cout << std::to_string(to_us_since_boot(time)) << "] ";
    std::cout.flags(flags);
}

void print_critical(const std::string& msg) {
  if (debugLevel >= DebugLevel::Critical) {
    print_timestamp();
    std::cout <<  "(CRIT ) " << msg << std::endl;
  }
}

void print_warn(const std::string& msg) {
  if (debugLevel >= DebugLevel::Warnings) {
    print_timestamp();
    std::cout <<  "(WARN ) " << msg << std::endl;
  }
}

void print_message(const std::string& msg) {
  if (debugLevel >= DebugLevel::Info) {
    print_timestamp();
    std::cout <<  "(INFO ) " << msg << std::endl;
  }
}

void print_debug(const std::string& msg) {
  if (debug_enabled()) {
    print_timestamp();
    std::cout <<  "(DEBUG) " << msg << std::endl;
  }
}

bool debug_enabled() {
  return debugLevel >= DebugLevel::Debug;
}

const char* level_name() {
  constexpr const char* names[] = {"SILENT", "CRITICAL", "WARNINGS", "INFO", "DEBUG"};
  return names[static_cast<int>(debugLevel)];
}

void print_welcome() {
  std::cout << std::endl;
  std::cout << "*-----------------------------------------------------------------------------*\n"
            << "*                                  SMC Boot                                   *\n"
            << "*-----------------------------------------------------------------------------*"
            << std::endl;
}

} // namespace debug
