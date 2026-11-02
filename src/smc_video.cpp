#include "smc_video.hpp"
#include "smc.hpp"
#include "debug.hpp"
#include "hal.hpp"

#include <cstdio>

namespace SMC {
namespace Video {

flags_struct   flags;
sensors_struct sensors;

// Same as the PIC (0x851). The mode pins read av_pack::none (all high) with no
// AV cable, and av_pack::special while unplugged acts like a power button press.

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
constexpr bool    override = SMC_AV_PACK_OVERRIDE;
constexpr av_pack override_mode = static_cast<av_pack>((SMC_AV_PACK_MODE & 0x07) << 1);
video_state       state;
av_pack           vmode_raw = av_pack::scart;
av_pack           vmode_prev = av_pack::scart;
bool              mode_printed = false;  // Cable printed since power-up
uint8_t           settle_ticks = 0;      // Let the AV pack power up before trusting an unchanged mode

void init() {
  state = video_state::monitor;
  mode_printed = false;
  settle_ticks = 10;
  vmode_raw = av_pack::scart;
  vmode_prev = av_pack::scart;
}

static av_pack readMode() {
  const av_pack mode = override ? override_mode : static_cast<av_pack>(hal::get_video_mode());

  // Raw pin changes, to see what the AV port reads when a cable is swapped
  static uint8_t last_logged = 0xFF;
  const uint8_t  pins = static_cast<uint8_t>(mode);
  if (pins != last_logged && debug::debug_enabled()) {
    char msg[48];
    snprintf(msg, sizeof(msg), "VIDEO: Pins 0x%02X (state %u)", pins, static_cast<unsigned>(state));
    debug::print_debug(msg);
  }
  last_logged = pins;

  return mode;
}

// The AV pack's ID pins are only meaningful while the Xbox is on
static void printModeIfPowered(av_pack mode) {
  if (hal::get_power_ok()) {
    printMode(mode);
    mode_printed = true;
  }
}

static void setMode(av_pack mode) {
  if (mode != sensors.vmode) {
    printModeIfPowered(mode);
  }
  sensors.vmode = mode;
}

void update() {
  switch (state) {
    case video_state::monitor:
      vmode_raw = readMode();
      if (vmode_raw == av_pack::none) {
        state = video_state::cable_removed;
      } else if (vmode_raw != sensors.vmode) {
        state = video_state::changed;
      } else if (settle_ticks != 0) {
        settle_ticks--;
      } else if (!mode_printed) {
        printModeIfPowered(vmode_raw);  // Unchanged since standby, print it once
      }
      break;

    case video_state::changed:
      flags.av_cable_missing = false;
      vmode_prev = vmode_raw;
      state = video_state::debounce;
      break;

    case video_state::debounce:
      vmode_raw = readMode();
      if (vmode_raw == sensors.vmode) {
        state = video_state::monitor;  // Back to the old mode, ignore
      } else if (vmode_raw == vmode_prev) {
        state = video_state::confirmed;
      } else {
        state = video_state::changed;
      }
      break;

    case video_state::confirmed:
      setMode(vmode_raw);
      SMI::flags.video_mode_changed = true;  // SMI to the OS
      state = video_state::monitor;
      break;

    case video_state::cable_removed:
      // Debounced like P2L (0x11DC): back to watching unless it still reads no
      // cable. The PIC means to do the same, but compares vmode_raw with itself,
      // so it accepts the removal right away.
      vmode_prev = readMode();
      if (vmode_prev != av_pack::none) {
        state = video_state::monitor;
        break;
      }
      flags.av_cable_missing = true;         // No AV cable (LEDs, audio clamp)
      SMI::flags.av_unplugged_event = true;  // SMI to the OS
      setMode(vmode_raw);
      debug::print_message("VIDEO: Unplugged, SMI queued, audio clamped, LEDs orange/green");
      state = video_state::no_cable;
      break;

    case video_state::no_cable:
      vmode_raw = readMode();
      if (vmode_raw == av_pack::special) {
        state = video_state::special_debounce;
      } else if (vmode_raw != av_pack::none) {
        /* Cable is back */
        debug::print_message("VIDEO: Cable back, audio unclamped");
        flags.av_cable_missing = false;
        AudioClamp::flags.audio_unclamp_request = true;  // Request unclamp
        state = video_state::monitor;
      }
      break;

    case video_state::special_debounce:
      vmode_raw = readMode();
      if (vmode_raw == av_pack::special) {
        SMI::flags.av_power_button_event = true;  // SMI as power button
        flags.av_cable_missing = false;
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

void printMode(const av_pack vm) {
  std::string message = "VIDEO: AV Cable [";
  bool        recognized_cable = true;

  switch (vm) {
    case av_pack::scart:
      message += "Advanced SCART Cable";
      break;
    case av_pack::hdtv:
      message += "High Definition AV Pack";
      break;
    case av_pack::vga:
      message += "VGA / progressive RGB";
      break;
    case av_pack::rf:
      message += "RF Adapter";
      break;
    case av_pack::advanced:
      message += "Advanced AV Pack";
      break;
    case av_pack::special:
      message += "Unknown mode 5";
      recognized_cable = false;
      break;
    case av_pack::standard:
      message += "Standard AV Cable";
      break;
    case av_pack::none:
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

}  // namespace Video
}  // namespace SMC
