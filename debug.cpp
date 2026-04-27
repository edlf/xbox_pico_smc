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
            << "* video_mode        [" << static_cast<int>(state.video_mode)          << "]\n"
            << "* audio_clamp       [" << static_cast<int>(state.audio_clamp)         << "]\n"
            << "* pwr_sw            [" << static_cast<int>(state.pwr_sw)              << "]\n"
            << "* boot_challenge    [" << static_cast<int>(state.boot_challenge)      << "]\n"
            << "* dvd_tray_3        [" << static_cast<int>(state.dvd_tray_3)          << "]\n"
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

void print_state_changes(const SMC::state_struct& state, const SMC::state_struct& state_previous) {
  if (state.standby_power != state_previous.standby_power) {
    print_state_change("standby_power", static_cast<uint8_t>(state_previous.standby_power), static_cast<uint8_t>(state.standby_power));
  }
  if (state.smi_power != state_previous.smi_power) {
    print_state_change("smi_power", static_cast<uint8_t>(state_previous.smi_power), static_cast<uint8_t>(state.smi_power));
  }
  if (state.fan_control != state_previous.fan_control) {
    print_state_change("fan_control", static_cast<uint8_t>(state_previous.fan_control), static_cast<uint8_t>(state.fan_control));
  }
  if (state.dvd_tray != state_previous.dvd_tray) {
    print_state_change("dvd_tray", static_cast<uint8_t>(state_previous.dvd_tray), static_cast<uint8_t>(state.dvd_tray));
  }
  if (state.tray_eject != state_previous.tray_eject) {
    print_state_change("tray_eject", static_cast<uint8_t>(state_previous.tray_eject), static_cast<uint8_t>(state.tray_eject));
  }
  if (state.video_mode != state_previous.video_mode) {
    print_state_change("video_mode", static_cast<uint8_t>(state_previous.video_mode), static_cast<uint8_t>(state.video_mode));
  }
  if (state.audio_clamp != state_previous.audio_clamp) {
    print_state_change("audio_clamp", static_cast<uint8_t>(state_previous.audio_clamp), static_cast<uint8_t>(state.audio_clamp));
  }
  if (state.pwr_sw != state_previous.pwr_sw) {
    print_state_change("pwr_sw", static_cast<uint8_t>(state_previous.pwr_sw), static_cast<uint8_t>(state.pwr_sw));
  }
  if (state.boot_challenge != state_previous.boot_challenge) {
    print_state_change("boot_challenge", static_cast<uint8_t>(state_previous.boot_challenge), static_cast<uint8_t>(state.boot_challenge));
  }
  if (state.dvd_tray_3 != state_previous.dvd_tray_3) {
    print_state_change("dvd_tray_3", static_cast<uint8_t>(state_previous.dvd_tray_3), static_cast<uint8_t>(state.dvd_tray_3));
  }
  if (state.pll_reset != state_previous.pll_reset) {
    print_state_change("pll_reset", static_cast<uint8_t>(state_previous.pll_reset), static_cast<uint8_t>(state.pll_reset));
  }
  if (state.eject_sw != state_previous.eject_sw) {
    print_state_change("eject_sw", static_cast<uint8_t>(state_previous.eject_sw), static_cast<uint8_t>(state.eject_sw));
  }
  if (state.update_eject_tray != state_previous.update_eject_tray) {
    print_state_change("update_eject_tray", static_cast<uint8_t>(state_previous.update_eject_tray), static_cast<uint8_t>(state.update_eject_tray));
  }
}

} // namespace debug
