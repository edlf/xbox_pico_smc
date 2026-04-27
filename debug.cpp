#include "debug.hpp"
#include <iostream>
#include <iomanip>
#include "pico/time.h"

namespace debug {

DebugLevel debugLevel = DebugLevel::All;

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

void print_error(const std::string& msg) {
  if (debugLevel >= DebugLevel::Errors) {
    print_timestamp();
    std::cout <<  "(ERROR) " << msg << std::endl;
  }
}

void print_warn(const std::string& msg) {
  if (debugLevel >= DebugLevel::Warnings) {
    print_timestamp();
    std::cout <<  "(WARN ) " << msg << std::endl;
  }
}

void print_message(const std::string& msg) {
  if (debugLevel == DebugLevel::All) {
    print_timestamp();
    std::cout <<  "(INFO ) " << msg << std::endl;
  }
}

void print_welcome() {
  std::cout << std::endl;
  std::cout << "*-----------------------------------------------------------------------------*\n"
            << "*                                  SMC Boot                                   *\n"
            << "*-----------------------------------------------------------------------------*"
            << std::endl;
}

void print_state_change(const std::string state_name, const uint8_t previous, const uint8_t current) {
  if (debugLevel == DebugLevel::All) {
    print_timestamp();
    std::cout <<  "(STATE) " << state_name;

    constexpr size_t max_len = 20;
    size_t str_size = state_name.size();
    if (max_len > str_size) {
      std::string s(max_len - str_size, ' ');
      std::cout << s;
    }

    std::cout << " " << std::to_string(previous) << " -> " << std::to_string(current) << std::endl;
  }
}

} // namespace debug
