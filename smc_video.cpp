#include "smc_video.hpp"
#include "smc.hpp"
#include "debug.hpp"
#include "hal.hpp"

namespace SMC {
namespace Video {

bool override = false;
uint8_t override_mode = 0b00000010;
uint8_t video_mode;
uint8_t previous_video_mode;

void init() {
  video_mode = 0;
  previous_video_mode = 200;
}

void update() {
  if (!SMI::isXboxPowered()) {
    return;
  }

  if (override) {
    SMC::sensors.vmode_raw = override_mode;
  } else {
    SMC::sensors.vmode_raw = hal::get_video_mode();
  }

  if (previous_video_mode != SMC::sensors.vmode_raw) {
    printMode(SMC::sensors.vmode_raw);
    previous_video_mode = SMC::sensors.vmode_raw;
  }

  if (SMC::sensors.vmode_raw != SMC::sensors.vmode) {
    SMC::sensors.vmode = SMC::sensors.vmode_raw;
    SMC::setStatusBit(SMC::video_mode_changed);
  }
}

void printMode(const uint8_t vm) {
  // TODO this is only valid if xbox in on

  std::string message = "VIDEO: AV Cable [";
  bool recognized_cable = true;

  uint8_t temp = (vm >> 1);
  switch (temp) {
    case 0:
      message += "Advanced SCART Cable";
      break;
    case 1:
      message += "High Definition AV Pack";
      break;
    case 2:
      message += "VGA / progressive RGB";
      break;
    case 3:
      message += "RF Adapter";
      break;
    case 4:
      message += "Advanced AV Pack";
      break;
    case 5:
      message += "Unknown mode 5";
      recognized_cable = false;
      break;
    case 6:
      message += "Standard AV Cable";
      break;
    case 7:
      message += "No cable connected";
      recognized_cable = false;
      break;
    default:
      message += "Invalid video mode";
      recognized_cable = false;
      break;
  }

  message += "]";

  if (recognized_cable) {
    debug::print_message(message);
  } else {
    debug::print_warn(message);
  }
}

} // namespace Video
} // namespace SMC
