#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"
#include "pico/stdlib.h"
#include "pin_assignments.hpp"
#include "smc_video.hpp"
#include "smc_front_panel_sw.hpp"
#include "smc_dvd.hpp"

namespace SMC {
namespace PowerStandby {

power_standby_state state;

void init(){
  state = power_standby_state::initial;
}

uint8_t update() {
  // Done, untested, some port stuff missing
  switch (state) {
  case power_standby_state::initial: // State 0
    hal::led_red_off();
    hal::led_green_on();
    hal::set_fan_off();
    hal::timer1_init(500);
    sensors.tray_status = 0;
    sensors.vmode = 0x0A;
    state = power_standby_state::read_av;
    break;

  case power_standby_state::read_av: // State 1
    Video::update();
    state = power_standby_state::check_av_power_button;
    break;

  case power_standby_state::check_av_power_button: // State 2
    // Check power button or AVIP port kiosk power on
    FrontPanelSW::updatePower();
    if ((sensors.vmode != 0x0E) && (!checkStatusBit(power_change_requested))) {
      state = power_standby_state::check_eject_button;
    } else {
      state = power_standby_state::turn_on_power_no_av;
    }
    break;

  case power_standby_state::turn_on_power_no_av: // State 3
    hal::turn_on_psu();
    hal::set_fan_on();
    timers.power_timeout3 = 50;
    clearStatusBit(power_change_requested);
    state = power_standby_state::powered_up_wait_power_ok;
    break;

  case power_standby_state::powered_up_wait_power_ok: // State 4
    hal::petWatchdog();
    hal::timer0_init(500);

    if (hal::get_power_ok()) {
      state = power_standby_state::powered_up;
    } else {
      if (--timers.power_timeout3 == 0) {
        panic("POWER_OK timeout");
      } else {
        hal::timer1_wait();
      }
    }
    break;

  case power_standby_state::check_eject_button: // State 5
    FrontPanelSW::updateEject();
    if (checkStatusBit(eject_change_requested)) {
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
    flags.bitfield_DATA_73 |= 0x02;                    // Set eject flag
    hal::turn_on_psu();
    hal::set_fan_on();
    timers.power_timeout3 = 0x32; // 50 cycles
    state = power_standby_state::powered_up_wait_power_ok_alternative;
    break;

  case power_standby_state::powered_up_wait_power_ok_alternative:
    hal::petWatchdog();
    hal::timer1_init(500);

    if (hal::get_power_ok()) {
      state = power_standby_state::powered_up_alt;
    } else if (--timers.power_timeout3 == 0) {
      panic("POWER_OK timeout");
    } else {
      hal::timer1_wait();
    }
    break;

  case power_standby_state::powered_up_alt:
    clearStatusBit(eject_change_requested);
    setInterruptReason(InterruptReason_eject_sw_pressed);
    utils::setBitNo(flags.bitfield_DATA_6D, 5);
    isr();
    state = power_standby_state::powered_up;
    break;

  case power_standby_state::powered_up:
    utils::setBitNo(flags.bitfield_DATA_70, 1);
    utils::clearBitNo(flags.bitfield_DATA_70, 2);
    setStatusBit(first_execution);
    clearStatusBit(video_mode_changed);
    utils::clearBitNo(flags.bitfield_DATA_6F, 0);
    utils::clearBitNo(flags.bitfield_DATA_6F, 7);
    setStatusBit(audio_clamp_timer);
    Video::init();
    Dvd::initDdvdTray();
    ram_test_response0 = 0;
    ram_test_response1 = 0;
    hal::timer1_init(500);
    hal::setupI2C();
    gpio_set_irq_enabled_with_callback(pins::POWER_OK, GPIO_IRQ_EDGE_FALL, true, &smc_gpio_callback);
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
