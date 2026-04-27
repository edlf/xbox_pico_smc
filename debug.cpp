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

void print_states(const SMC::state_struct& state) {
  std::cout << "*--------------- State info start ---------------*\n"
            << "* standby_power     [" << static_cast<int>(state.standby_power)       << "]\n"
            << "* power             [" << static_cast<int>(state.smi_power)           << "]\n"
            << "* fan_control       [" << static_cast<int>(state.fan_control)         << "]\n"
            << "* dvd_tray          [" << static_cast<int>(state.dvd_tray)            << "]\n"
            << "* tray_eject        [" << static_cast<int>(state.tray_eject)          << "]\n"
            << "* pwr_sw            [" << static_cast<int>(state.pwr_sw)              << "]\n"
            << "* dvd_tray_three    [" << static_cast<int>(state.dvd_tray_three)      << "]\n"
            << "* pll_reset         [" << static_cast<int>(state.pll_reset)           << "]\n"
            << "* eject_sw          [" << static_cast<int>(state.eject_sw)            << "]\n"
            << "* update_eject_tray [" << static_cast<int>(state.update_eject_tray)   << "]\n"
            << "*--------------- State info end -----------------*" << std::endl;
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
