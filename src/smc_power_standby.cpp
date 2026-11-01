#include "smc.hpp"
#include "hal.hpp"
#include "pico/stdlib.h"
#include "pin_assignments.hpp"
#include "smc_video.hpp"
#include "smc_front_panel_sw.hpp"
#include "smc_dvd.hpp"

namespace SMC {
namespace PowerStandby {

power_standby_state state;
static uint16_t power_timeout3 = 0;

void init(){
  state = power_standby_state::initial;
}

uint8_t update() {
  // Done, untested, some port stuff missing
  switch (state) {
  case power_standby_state::initial: // State 0
    hal::led_red_off();
    hal::led_green_off();
    hal::fan_pin_low();
    hal::timer1_init(hal::TICK_US);
    sensors.tray_status = drive_tray_state::closed;
    sensors.vmode = av_pack::special;
    state = power_standby_state::read_av;
    break;

  case power_standby_state::read_av: // State 1
    Video::update();
    state = power_standby_state::check_av_power_button;
    break;

  case power_standby_state::check_av_power_button: // State 2
    // Check power button or AVIP port kiosk power on
    FrontPanelSW::updatePower();
    if ((sensors.vmode != av_pack::none) && (!flags.power_change_requested)) {
      state = power_standby_state::check_eject_button;
    } else {
      state = power_standby_state::turn_on_power_no_av;
    }
    break;

  case power_standby_state::turn_on_power_no_av: // State 3
    hal::turn_on_psu();
    // The PIC holds the pin high (speed 0), P2L (0xCA5) starts at 30
    hal::set_fan_speed(SMC_FIRMWARE_P2L ? 30 : hal::FAN_MIN_SPEED);
    power_timeout3 = 50;
    flags.power_change_requested = false;
    state = power_standby_state::powered_up_wait_power_ok;
    break;

  case power_standby_state::powered_up_wait_power_ok: // State 4
    hal::petWatchdog();

    if (hal::get_power_ok()) {
      state = power_standby_state::powered_up;
    } else {
      if (--power_timeout3 == 0) {
        panic("POWER_OK timeout");
      } else {
        hal::timer1_wait();
      }
    }
    break;

  case power_standby_state::check_eject_button: // State 5
    FrontPanelSW::updateEject();
    if (flags.eject_change_requested) {
      state = power_standby_state::turn_on_power_alternative;
    } else {
      state = power_standby_state::idle;
    }
    break;

  case power_standby_state::idle: // State 6
    update_LEDs();
    hal::timer1_wait();
    state = power_standby_state::read_av; // Loop back to AV reading
    return 0;
    break;

  case power_standby_state::turn_on_power_alternative: // State 7
    flags.tray_open_expected = true; // Powered on by the eject button
    hal::turn_on_psu();
    // The PIC holds the pin high (speed 0), P2L (0xCA5) starts at 30
    hal::set_fan_speed(SMC_FIRMWARE_P2L ? 30 : hal::FAN_MIN_SPEED);
    power_timeout3 = 0x32; // 50 cycles
    state = power_standby_state::powered_up_wait_power_ok_alternative;
    break;

  case power_standby_state::powered_up_wait_power_ok_alternative:
    hal::petWatchdog();
    hal::timer1_init(hal::TICK_US);

    if (hal::get_power_ok()) {
      state = power_standby_state::powered_up_alt;
    } else if (--power_timeout3 == 0) {
      panic("POWER_OK timeout");
    } else {
      hal::timer1_wait();
    }
    break;

  case power_standby_state::powered_up_alt:
    flags.eject_change_requested = false;
    setInterruptReason(InterruptReason_eject_sw_pressed);
    flags.pending_interrupt_reason |= InterruptReason_eject_sw_pressed;
    fireSystemInterrupt();
    state = power_standby_state::powered_up;
    break;

  case power_standby_state::powered_up:
    flags.run_after_reset = true;
    flags.led_override = false;
    flags.first_execution = true;
    flags.video_mode_changed = false;
    flags.av_cable_missing = false;
    flags.audio_clamp_request = false;
    flags.audio_unclamp_request = false;
    flags.audio_clamp_timer = true;
    Video::init();
    Dvd::initDvdTray();
    ram_test_response0 = 0;
    ram_test_response1 = 0;
    hal::timer1_init(hal::TICK_US);
    setPowerOkIrq(true);
    return 1;
    break;

  default:
    state = power_standby_state::initial;
    break;
  }

  return 0;
}

} // namespace PowerStandby
} // namespace SMC
