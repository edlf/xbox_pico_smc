#include "smc_video.hpp"
#include "smc.hpp"
#include "debug.hpp"
#include "pico_hal.hpp"

namespace SMC {
namespace Video {

uint8_t video_mode;
uint8_t previous_video_mode;

void init() {
  video_mode = 0;
  previous_video_mode = 200;
}

void update() {
  SMC::sensors.vmode_raw = pico_hal::get_video_mode();

  if (previous_video_mode != SMC::sensors.vmode_raw) {
    printMode(SMC::sensors.vmode_raw);
    previous_video_mode = SMC::sensors.vmode_raw;
  }

  if (SMC::sensors.vmode_raw != SMC::sensors.vmode) {
    SMC::sensors.vmode = SMC::sensors.vmode_raw;
    SMC::setStatusBit(SMC::video_mode_changed); // Set video mode change flag
  }
}

void printMode(const uint8_t vm) {
  // TODO this is only valid if xbox in on
  uint8_t temp = (vm >> 1);
  switch (temp) {
    case 0:
      debug::print_message("Advanced SCART Cable");
      break;
    case 1:
      debug::print_message("High Definition AV Pack");
      break;
    case 2:
      debug::print_message("VGA / progressive RGB");
      break;
    case 3:
      debug::print_message("RF Adapter");
      break;
    case 4:
      debug::print_message("Advanced AV Pack");
      break;
    case 5:
      debug::print_message("Unknown mode 5");
      break;
    case 6:
      debug::print_message("Standard AV Cable");
      break;
    case 7:
      debug::print_message("No cable connected");
      break;
    default:
      debug::print_warn("Invalid video mode");
      break;
  }
}

} // namespace Video
} // namespace SMC
