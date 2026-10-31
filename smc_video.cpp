#include "smc_video.hpp"
#include "smc.hpp"
#include "debug.hpp"
#include "hal.hpp"
#include "utils.hpp"

namespace SMC {
namespace Video {

// Same as the PIC (0x851). Mode pins are read as PORTB & 0x0E: 0x0E (all
// high) means no AV cable, 0x0A while unplugged is the special mode that acts
// like a power button press.
constexpr uint8_t no_cable = 0x0E;
constexpr uint8_t special_mode = 0x0A;

enum class video_state {
  monitor,           // 0
  changed,           // 1
  debounce,          // 2
  confirmed,         // 3
  cable_removed,     // 4
  no_cable,          // 5
  special_debounce,  // 6
  special_done       // 7
};

// Fixed AV pack from CMake (SMC_AV_PACK_OVERRIDE), as mode pins: code << 1
constexpr bool override = SMC_AV_PACK_OVERRIDE;
constexpr uint8_t override_mode = (SMC_AV_PACK_MODE & 0x07) << 1;
video_state state;
uint8_t vmode_raw = 0;
uint8_t vmode_prev = 0;
bool mode_printed = false; // Cable printed since power-up
uint8_t settle_ticks = 0;  // Let the AV pack power up before trusting an unchanged mode

void init() {
  state = video_state::monitor;
  mode_printed = false;
  settle_ticks = 10;
  vmode_raw = 0;
  vmode_prev = 0;
}

static uint8_t readMode() {
  return override ? override_mode : hal::get_video_mode();
}

// The AV pack's ID pins are only meaningful while the Xbox is on
static void printModeIfPowered(uint8_t mode) {
  if (hal::get_power_ok()) {
    printMode(mode);
    mode_printed = true;
  }
}

static void setMode(uint8_t mode) {
  if (mode != sensors.vmode) {
    printModeIfPowered(mode);
  }
  sensors.vmode = mode;
}

void update() {
  switch (state) {
  case video_state::monitor:
    vmode_raw = readMode();
    if (vmode_raw == no_cable) {
      state = video_state::cable_removed;
    } else if (vmode_raw != sensors.vmode) {
      state = video_state::changed;
    } else if (settle_ticks != 0) {
      settle_ticks--;
    } else if (!mode_printed) {
      printModeIfPowered(vmode_raw); // Unchanged since standby, print it once
    }
    break;

  case video_state::changed:
    utils::clearBitNo(flags.bitfield_DATA_71, 6);
    vmode_prev = vmode_raw;
    state = video_state::debounce;
    break;

  case video_state::debounce:
    vmode_raw = readMode();
    if (vmode_raw == sensors.vmode) {
      state = video_state::monitor; // Back to the old mode, ignore
    } else if (vmode_raw == vmode_prev) {
      state = video_state::confirmed;
    } else {
      state = video_state::changed;
    }
    break;

  case video_state::confirmed:
    setMode(vmode_raw);
    setStatusBit(Status::video_mode_changed); // SMI to the OS
    state = video_state::monitor;
    break;

  case video_state::cable_removed:
    // The PIC means to debounce here, but compares vmode_raw with itself, so
    // the removal is always accepted right away
    vmode_prev = readMode();
    utils::setBitNo(flags.bitfield_DATA_71, 6); // No AV cable (LEDs, audio clamp)
    utils::setBitNo(flags.bitfield_DATA_71, 7); // SMI to the OS
    setMode(vmode_raw);
    state = video_state::no_cable;
    break;

  case video_state::no_cable:
    vmode_raw = readMode();
    if (vmode_raw == special_mode) {
      state = video_state::special_debounce;
    } else if (vmode_raw != no_cable) {
      /* Cable is back */
      utils::clearBitNo(flags.bitfield_DATA_71, 6);
      utils::setBitNo(flags.bitfield_DATA_6F, 7); // Request unclamp
      state = video_state::monitor;
    }
    break;

  case video_state::special_debounce:
    vmode_raw = readMode();
    if (vmode_raw == special_mode) {
      utils::setBitNo(flags.bitfield_DATA_72, 0); // SMI as power button
      utils::clearBitNo(flags.bitfield_DATA_71, 6);
      state = video_state::special_done;
    } else {
      state = video_state::no_cable;
    }
    break;

  case video_state::special_done:
    /* Stays here until the next init */
    break;
  }
}

void printMode(const uint8_t vm) {
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
