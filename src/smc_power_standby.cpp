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
static uint16_t     power_timeout3 = 0;

// The Pico's onboard LED breathes while the Xbox is off (solid while it runs)
constexpr uint8_t breathe_ticks = 200;  // 4 s per breath
static uint8_t    breathe_step = 0;

static void breathePicoLed() {
  const uint8_t half = breathe_ticks / 2;
  const uint8_t ramp = breathe_step < half ? breathe_step : breathe_ticks - breathe_step;
  hal::set_pico_led_level(static_cast<uint8_t>(255 * ramp / half));
  if (++breathe_step >= breathe_ticks) {
    breathe_step = 0;
  }
}

void init() {
  state = power_standby_state::initial;
}

uint8_t update() {
  // Done, untested, some port stuff missing
  switch (state) {
    case power_standby_state::initial:  // State 0
      hal::led_red_off();
      hal::led_green_off();
      hal::fan_pin_low();
      hal::timer1_init(hal::TICK_US);
      breathe_step = 0;  // Start from dark
      Dvd::sensors.tray_status = Dvd::drive_tray_state::closed;
      Video::sensors.vmode = Video::av_pack::special;
      state = power_standby_state::read_av;
      break;

    case power_standby_state::read_av:  // State 1
      Video::update();
      state = power_standby_state::check_av_power_button;
      break;

    case power_standby_state::check_av_power_button:  // State 2
      // Check power button or AVIP port kiosk power on
      FrontPanelSW::updatePower();
      if ((Video::sensors.vmode != Video::av_pack::none) && (!SMI::flags.power_change_requested)) {
        state = power_standby_state::check_eject_button;
      } else {
        state = power_standby_state::turn_on_power_no_av;
      }
      break;

    case power_standby_state::turn_on_power_no_av:  // State 3
      hal::turn_on_psu();
      // The PIC holds the pin high (speed 0), P2L starts at 30
      hal::set_fan_speed(SMC_FIRMWARE_P2L ? 30 : hal::FAN_MIN_SPEED);
      power_timeout3 = 50;
      SMI::flags.power_change_requested = false;
      state = power_standby_state::powered_up_wait_power_ok;
      break;

    case power_standby_state::powered_up_wait_power_ok:  // State 4
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

    case power_standby_state::check_eject_button:  // State 5
      FrontPanelSW::updateEject();
      if (Dvd::flags.eject_change_requested) {
        state = power_standby_state::turn_on_power_alternative;
      } else {
        state = power_standby_state::idle;
      }
      break;

    case power_standby_state::idle:  // State 6
      update_LEDs();
      breathePicoLed();
      ExtraTemp::update();
      I2CSniffer::update();
      Settings::update();  // Only here: saving pauses both cores
      hal::timer1_wait();
      state = power_standby_state::read_av;  // Loop back to AV reading
      return 0;
      break;

    case power_standby_state::turn_on_power_alternative:  // State 7
      Dvd::flags.tray_open_expected = true;               // Powered on by the eject button
      hal::turn_on_psu();
      // The PIC holds the pin high (speed 0), P2L starts at 30
      hal::set_fan_speed(SMC_FIRMWARE_P2L ? 30 : hal::FAN_MIN_SPEED);
      power_timeout3 = 0x32;  // 50 cycles
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
      Dvd::flags.eject_change_requested = false;
      SMI::raiseInterrupt(SMI::InterruptReason_eject_sw_pressed);
      state = power_standby_state::powered_up;
      break;

    case power_standby_state::powered_up:
      hal::pico_led_on();  // Solid while the Xbox runs
      PLL_Reset::flags.run_after_reset = true;
      Led::flags.led_override = false;
      SMI::flags.first_execution = true;
      SMI::flags.video_mode_changed = false;
      Video::flags.av_cable_missing = false;
      AudioClamp::flags.audio_clamp_request = false;
      AudioClamp::flags.audio_unclamp_request = false;
      AudioClamp::flags.audio_clamp_timer = true;
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

}  // namespace PowerStandby
}  // namespace SMC
