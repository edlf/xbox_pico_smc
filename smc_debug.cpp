#include "smc_debug.hpp"

namespace SMC {
namespace debug {

void print_states(const state_struct& state) {
  printf("--------------- State info start ---------------\n");
  printf("standby_power     %d\n", state.standby_power);
  printf("power             %d\n", state.smi_power);
  printf("fan_control       %d\n", state.fan_control);
  printf("dvd_tray          %d\n", state.dvd_tray);
  printf("tray_eject        %d\n", state.tray_eject);
  printf("video_mode        %d\n", state.video_mode);
  printf("audio_clamp       %d\n", state.audio_clamp);
  printf("pwr_sw            %d\n", state.pwr_sw);
  printf("boot_challenge    %d\n", state.boot_challenge);
  printf("leds              %d\n", state.leds);
  printf("dvd_tray_3        %d\n", state.dvd_tray_3);
  printf("pll_reset         %d\n", state.pll_reset);
  printf("eject_sw          %d\n", state.eject_sw);
  printf("update_eject_tray %d\n", state.update_eject_tray);
  printf("---------------- State info end ----------------\n");
}

void print_state_changes(const state_struct& state, const state_struct& state_previous) {
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

} // namespace SMC
} // namespace debug
