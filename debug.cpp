#include "debug.hpp"
#include <iostream>

namespace debug {

DebugLevel debugLevel = DebugLevel::All;

void print_critical(const std::string& msg) {
  if (debugLevel >= DebugLevel::Critical) {
    std::cout << "(CRIT) " << msg << std::endl;
  }
}

void print_error(const std::string& msg) {
  if (debugLevel >= DebugLevel::Errors) {
    std::cout << "(ERR ) " << msg << std::endl;
  }
}

void print_warn(const std::string& msg) {
  if (debugLevel >= DebugLevel::Warnings) {
    std::cout << "(WARN) " << msg << std::endl;
  }
}

void print_message(const std::string& msg) {
  if (debugLevel == DebugLevel::All) {
    std::cout << "(INFO) " << msg << std::endl;
  }
}

void print_welcome() {
  std::cout << "*------------------------------------------------*\n"
            << "*                    SMC Boot                    *\n"
            << "*------------------------------------------------*"
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
            << "* leds              [" << static_cast<int>(state.leds)                << "]\n"
            << "* dvd_tray_3        [" << static_cast<int>(state.dvd_tray_3)          << "]\n"
            << "* pll_reset         [" << static_cast<int>(state.pll_reset)           << "]\n"
            << "* eject_sw          [" << static_cast<int>(state.eject_sw)            << "]\n"
            << "* update_eject_tray [" << static_cast<int>(state.update_eject_tray)   << "]\n"
            << "*--------------- State info end -----------------*" << std::endl;
}

void print_state_changes(const SMC::state_struct& state, const SMC::state_struct& state_previous) {
  if (state.standby_power != state_previous.standby_power) {
    printf("standby_power     %02d -> %02d\n", state_previous.standby_power, state.standby_power);
  }
  if (state.smi_power != state_previous.smi_power) {
    printf("power             %02d -> %02d\n", state_previous.smi_power, state.smi_power);
  }
  if (state.fan_control != state_previous.fan_control) {
    printf("fan_control       %02d -> %02d\n", state_previous.fan_control, state.fan_control);
  }
  if (state.dvd_tray != state_previous.dvd_tray) {
    printf("dvd_tray          %02d -> %02d\n", state_previous.dvd_tray, state.dvd_tray);
  }
  if (state.tray_eject != state_previous.tray_eject) {
    printf("tray_eject        %02d -> %02d\n", state_previous.tray_eject, state.tray_eject);
  }
  if (state.video_mode != state_previous.video_mode) {
    printf("video_mode        %02d -> %02d\n", state_previous.video_mode, state.video_mode);
  }
  if (state.audio_clamp != state_previous.audio_clamp) {
    printf("audio_clamp       %02d -> %02d\n", state_previous.audio_clamp, state.audio_clamp);
  }
  if (state.pwr_sw != state_previous.pwr_sw) {
    printf("pwr_sw            %02d -> %02d\n", state_previous.pwr_sw, state.pwr_sw);
  }
  if (state.boot_challenge != state_previous.boot_challenge) {
    printf("boot_challenge    %02d -> %02d\n", state_previous.boot_challenge, state.boot_challenge);
  }
  if (state.leds != state_previous.leds) {
    printf("leds              %02d -> %02d\n", state_previous.leds, state.leds);
  }
  if (state.dvd_tray_3 != state_previous.dvd_tray_3) {
    printf("dvd_tray_3        %02d -> %02d\n", state_previous.dvd_tray_3, state.dvd_tray_3);
  }
  if (state.pll_reset != state_previous.pll_reset) {
    printf("pll_reset         %02d -> %02d\n", state_previous.pll_reset, state.pll_reset);
  }
  if (state.eject_sw != state_previous.eject_sw) {
    printf("eject_sw          %02d -> %02d\n", state_previous.eject_sw, state.eject_sw);
  }
  if (state.update_eject_tray != state_previous.update_eject_tray) {
    printf("update_eject_tray %02d -> %02d\n", state_previous.update_eject_tray, state.update_eject_tray);
  }
  // printf("---------------- State changes end ----------------\n");
}

} // namespace debug
