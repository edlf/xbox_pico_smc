#include "smc_encoder_mode.hpp"
#include "smc.hpp"
#include "utils.hpp"

#include <cstdio>
#include <string>

namespace SMC {
namespace EncoderMode {

// Register meanings and observed values are from X-HD Redux's passive SMBus
// probe (src/application/smbus_probe.c), which has them from the CX25870/871
// and FS454 datasheets and bus captures.

constexpr uint8_t address_conexant = 0x45;
constexpr uint8_t address_focus = 0x6A;
constexpr uint8_t address_xcalibur = 0x70;

// A mode set is a burst of writes: log once the encoder has been left alone
// this long
constexpr uint8_t settle_ticks = 10;  // 200 ms

// Conexant CX25870/871: registers auto-increment within a write
namespace cx {
constexpr uint8_t raster_sel = 0x2E;  // [1:0] RASTER_SEL (00 = SD interlaced), [7] HDTV_EN. 480p writes 0xAD
constexpr uint8_t in_mode_hi = 0x32;  // [3] = IN_MODE[3]
constexpr uint8_t wss_data = 0x60;    // [3:0] WSS aspect: PAL 0x8 4:3 / 0x7 16:9, NTSC 0x0 4:3 / 0x1 16:9
constexpr uint8_t hactive_lo = 0x78;  // H_ACTIVE[7:0], written on SD mode sets only
constexpr uint8_t hactive_hi = 0x86;  // [6:4] = H_ACTIVE[10:8]
constexpr uint8_t in_mode_lo = 0xC6;  // [2:0] = IN_MODE[2:0]. RGB: 0, 1, 2 or 8, anything else YCrCb
}  // namespace cx

// Focus FS454: registers auto-increment within a write
namespace fs {
constexpr uint8_t iho_lo = 0x00;     // Input offset [7:0]: SD 720-wide 103 (PAL) / 143 (NTSC), any other 640-wide
constexpr uint8_t ihw_lo = 0x04;     // Input width [7:0]
constexpr uint8_t ihw_hi = 0x05;     // Input width [9:8]
constexpr uint8_t wss_b0 = 0x83;     // 525 lines: 0x18 4:3 / 0x60 16:9. 625 lines: 0x00
constexpr uint8_t wss_b2 = 0x85;     // 625 lines: 0x08 4:3 / 0x07 16:9
constexpr uint8_t vid_cntl0 = 0x92;  // [2] YUV matrix bypass (set: YCrCb), [7] interlaced input
}  // namespace fs

// Xcalibur: SMBus block writes, a length byte (4) then a 32-bit value, LSB
// first, all to the one register
namespace xc {
constexpr uint8_t aspect = 0x07;      // Low byte: NTSC 0x00 4:3 / 0x02 16:9, PAL 0x10 4:3 / 0x0E 16:9, 0 on 480p/HD
constexpr uint8_t in_raster = 0x50;   // htotal << 16 | SDTV << 15 | vtotal
constexpr uint8_t width = 0x52;       // [31:24] active width / 16
constexpr uint8_t out_raster = 0x54;  // As 0x50
constexpr uint8_t mode_sel = 0x56;    // Low byte 0x8A on PAL 720-wide 0x04/0x06. Written last in a mode set
}  // namespace xc

static uint8_t     encoder = 0;      // Address of the encoder written to
static bool        changed = false;  // A mode register was written since the last log
static uint8_t     quiet_ticks = 0;  // Ticks since the last encoder write
static std::string last_logged;

// Conexant state
static bool     cx_raster_valid = false;
static uint8_t  cx_raster = 0;
static uint8_t  cx_in_mode = 0;
static uint8_t  cx_in_mode_have = 0;  // Bit 0: 0xC6 seen, bit 1: 0x32 seen
static uint16_t cx_hactive = 0x0200;  // [10:8] is always 2, see X-HD
static bool     cx_hactive_valid = false;
static uint8_t  cx_wss = 0;
static bool     cx_wss_valid = false;

// Focus state
static bool     fs_cntl_valid = false;
static uint8_t  fs_cntl = 0;
static uint16_t fs_ihw = 720;  // Power-on default
static bool     fs_ihw_valid = false;
static uint8_t  fs_iho = 0;
static bool     fs_iho_valid = false;
static uint8_t  fs_wss_b0 = 0;
static uint8_t  fs_wss_b2 = 0;
static bool     fs_wss_valid = false;

// Xcalibur state
static uint32_t xc_value = 0;
static uint32_t xc_in_raster = 0;
static uint32_t xc_width = 0;
static uint32_t xc_out_raster = 0;
static uint8_t  xc_mode_index = 0;  // 0: none yet or unknown
static uint8_t  xc_aspect = 0;
static bool     xc_aspect_valid = false;

static void resetXhd();

void reset() {
  resetXhd();
  encoder = 0;
  changed = false;
  quiet_ticks = 0;
  last_logged.clear();
  cx_raster_valid = false;
  cx_in_mode_have = 0;
  cx_hactive = 0x0200;
  cx_hactive_valid = false;
  cx_wss_valid = false;
  fs_cntl_valid = false;
  fs_ihw = 720;
  fs_ihw_valid = false;
  fs_iho_valid = false;
  fs_wss_valid = false;
  xc_mode_index = 0;
  xc_aspect_valid = false;
}

// The console's mode index (as in an X-HD mode packet), from the Xcalibur's
// raster registers. 0x0F, 0x10 and 0x11 are register-identical to 0x02, 0x04
// and 0x08 and read as those.
static uint8_t xcaliburModeIndex(const uint8_t mode_sel) {
  const uint16_t vtotal = xc_in_raster & 0x7FFF;
  const bool     sdtv = (xc_in_raster & 0x8000) != 0;
  const uint8_t  width16 = static_cast<uint8_t>(xc_width >> 24);
  const bool     narrow = width16 == 0x28;  // 640

  switch (vtotal) {
    case 0x20D:  // 525 lines
      return sdtv ? (narrow ? 0x01 : 0x02) : (narrow ? 0x07 : 0x08);

    case 0x271:  // 625 lines
      if ((xc_out_raster & 0x7FFF) == 0x208) {
        return narrow ? 0x03 : 0x04;
      }
      if (narrow) {
        return 0x05;
      }
      return mode_sel == 0x8A ? 0x06 : 0x12;

    case 0x2EE:  // 750 lines
      return 0x0A;

    case 0x465:                              // 1125 lines
      return width16 == 0x5A ? 0x0C : 0x0D;  // 1440 or 1920 wide

    default:
      return 0;
  }
}

// What the console's mode indexes are (X-HD Redux, src/shared/xbox_mode_index.c)
struct ModeInfo {
  uint8_t     index;
  const char* resolution;
  const char* standard;  // nullptr for 480p and HD
};

constexpr ModeInfo mode_infos[] = {
    {0x01, "640x480i",             "NTSC"     },
    {0x02, "720x480i",             "NTSC"     },
    {0x03, "640x480i",             "PAL 50 Hz"},
    {0x04, "720x480i",             "PAL 50 Hz"},
    {0x05, "640x576i",             "PAL"      },
    {0x06, "720x576i",             "PAL"      },
    {0x07, "720x480p",             nullptr    }, // 640x480p on the Xcalibur, see modeInfo()
    {0x08, "720x480p",             nullptr    },
    {0x09, "960x720p",             nullptr    },
    {0x0A, "1280x720p",            nullptr    },
    {0x0B, "1280x720p",            nullptr    },
    {0x0C, "1440x1080i",           nullptr    },
    {0x0D, "1920x1080i",           nullptr    },
    {0x0E, "1920x1080i",           nullptr    },
    {0x0F, "640x480i (1.1:1 PAR)", "NTSC"     },
    {0x10, "640x480i (1.1:1 PAR)", "PAL 50 Hz"},
    {0x11, "720x480p",             nullptr    },
    {0x12, "640x576i (1.1:1 PAR)", "PAL"      },
};

static const ModeInfo* modeInfo(const uint8_t index, const bool xcalibur) {
  static constexpr ModeInfo xcalibur_480p_640 = {0x07, "640x480p", nullptr};
  if (index == 0x07 && xcalibur) {
    return &xcalibur_480p_640;  // A real 640-wide raster there, 720-wide on the others
  }

  for (const ModeInfo& m : mode_infos) {
    if (m.index == index) {
      return &m;
    }
  }

  return nullptr;
}

// The parts of a mode, joined into the log line. Empty parts are left out.
struct Mode {
  std::string resolution;  // e.g. 720x480i
  std::string standard;    // NTSC or PAL
  std::string aspect;      // 4:3 or 16:9
  std::string colour;      // RGB or YCrCb
  std::string source;      // Encoder and raw values, to check the decode

  std::string text() const {
    std::string s = resolution.empty() ? "unknown resolution" : resolution;
    for (const std::string* part : {&standard, &aspect, &colour}) {
      if (!part->empty()) {
        s += ", " + *part;
      }
    }

    return s + " (" + source + ")";
  }
};

// WSS aspect and line standard. PAL (625 lines) uses ETSI EN 300 294, NTSC
// (525 lines, also PAL-60) EIAJ CPR-1204; they encode the aspect differently.
static void wssMode(Mode& mode, const bool pal, const bool widescreen) {
  mode.standard = pal ? "PAL" : "NTSC";
  mode.aspect = widescreen ? "16:9" : "4:3";
}

static void conexantWrite(const uint8_t reg, const uint8_t value) {
  switch (reg) {
    case cx::raster_sel: {
      cx_raster = value;
      cx_raster_valid = true;
      const bool interlaced = (value & 0x03) == 0 && (value & 0x80) == 0;
      if (!interlaced) {
        // Progressive mode sets don't rewrite the WSS or H_ACTIVE, drop the SD values
        cx_wss_valid = false;
        cx_hactive_valid = false;
      }

      break;
    }

    case cx::in_mode_lo:
      cx_in_mode = (cx_in_mode & 0x08) | (value & 0x07);
      cx_in_mode_have |= 0x01;
      break;

    case cx::in_mode_hi:
      cx_in_mode = (cx_in_mode & 0x07) | (value & 0x08);
      cx_in_mode_have |= 0x02;
      break;

    case cx::hactive_lo:
      cx_hactive = (cx_hactive & 0xFF00) | value;
      cx_hactive_valid = true;
      break;

    case cx::hactive_hi:
      cx_hactive = (cx_hactive & 0x00FF) | (((value & 0x70) >> 4) << 8);
      break;

    case cx::wss_data:
      cx_wss = value;
      cx_wss_valid = true;
      break;

    default:
      return;
  }
  changed = true;
}

static void focusWrite(const uint8_t reg, const uint8_t value) {
  switch (reg) {
    case fs::vid_cntl0:
      fs_cntl = value;
      fs_cntl_valid = true;
      if (!(value & 0x80)) {
        fs_wss_valid = false;  // Progressive: no WSS, drop the SD values
        fs_iho_valid = false;
      }
      break;

    case fs::iho_lo:
      fs_iho = value;
      fs_iho_valid = true;
      break;

    case fs::ihw_lo:
      fs_ihw = (fs_ihw & 0xFF00) | value;
      fs_ihw_valid = true;
      break;

    case fs::ihw_hi:
      fs_ihw = (fs_ihw & 0x00FF) | ((value & 0x03) << 8);
      fs_ihw_valid = true;
      break;

    case fs::wss_b0:
      fs_wss_b0 = value;
      fs_wss_valid = true;
      break;

    case fs::wss_b2:
      fs_wss_b2 = value;
      break;

    default:
      return;
  }
  changed = true;
}

static void xcaliburWrite(const uint8_t reg, const uint8_t index, const uint8_t value) {
  if (index == 0 || index > 4) {
    return;  // The block length, or more than a 32-bit value
  }

  if (index == 1) {
    xc_value = 0;
  }

  xc_value |= (uint32_t)value << (8 * (index - 1));
  if (index < 4) {
    return;
  }

  switch (reg) {
    case xc::in_raster:
      xc_in_raster = xc_value;
      break;

    case xc::width:
      xc_width = xc_value;
      break;

    case xc::out_raster:
      xc_out_raster = xc_value;
      break;

    case xc::mode_sel:
      xc_mode_index = xcaliburModeIndex(xc_value & 0xFF);
      break;

    case xc::aspect:
      xc_aspect = xc_value & 0xFF;
      xc_aspect_valid = true;
      break;

    default:
      return;
  }
  changed = true;
}

// X-HD mode packet: an X-HD-aware BIOS or dashboard tells the X-HD (HDMI
// board, SMBus 0x69) the exact mode, so it's known on every encoder. Byte
// writes (X-HD Redux src/shared/defines.h, src/application/smbus_i2c.h):
// 129 = bank (index back to 0), 130 = index, 128 = a packet byte at
// bank:index (index then increments), 131 = 1 applies it. The packet:
// encoder (8-bit address), region, mode (32-bit, LSB first: [23:16] mode
// index, bit 28 16:9, bit 29 SCART RGB), title ID, AV info.
namespace xhd {
constexpr uint8_t address = 0x69;
constexpr uint8_t write_config = 128;
constexpr uint8_t write_config_bank = 129;
constexpr uint8_t write_config_index = 130;
constexpr uint8_t write_config_apply = 131;
constexpr uint8_t packet_size = 14;
}  // namespace xhd

static uint16_t xhd_bank = 0;
static uint16_t xhd_index = 0;
static uint8_t  xhd_packet[xhd::packet_size];
static uint32_t xhd_logged_mode = 0;

static void xhdPacketApplied() {
  const uint8_t  encoder_address = xhd_packet[0];
  const uint32_t mode_dword = xhd_packet[2] | (xhd_packet[3] << 8) | (xhd_packet[4] << 16) | ((uint32_t)xhd_packet[5] << 24);

  if (mode_dword == xhd_logged_mode) {
    return;  // The dashboard repeats it
  }

  xhd_logged_mode = mode_dword;

  const uint8_t mode_index = (mode_dword >> 16) & 0xFF;
  const bool    xcalibur = encoder_address == (address_xcalibur << 1);
  char          dword[12];
  snprintf(dword, sizeof(dword), "0x%08lX", (unsigned long)mode_dword);

  Mode mode;
  mode.source = "X-HD mode packet, mode " + utils::toHex(mode_index) + ", encoder " + utils::toHex(encoder_address) + ", mode dword " + dword;
  const ModeInfo* info = modeInfo(mode_index, xcalibur);

  if (info != nullptr) {
    mode.resolution = info->resolution;
    if (info->standard != nullptr) {
      mode.standard = info->standard;
    }

    // HD is 16:9 by format; on 480p the packet's bit isn't reliable (X-HD)
    if (mode_index >= 0x09 && mode_index <= 0x0E) {
      mode.aspect = "16:9";
    } else if (mode_index != 0x07 && mode_index != 0x08 && mode_index != 0x11) {
      mode.aspect = (mode_dword & 0x10000000) ? "16:9" : "4:3";
    }
  }

  if (mode_dword & 0x20000000) {
    mode.colour = "SCART RGB";
  }

  debug::print_message("ENCODER: Video mode " + mode.text());
}

static void resetXhd() {
  xhd_bank = 0;
  xhd_index = 0;
  xhd_logged_mode = 0;
}

static void xhdWrite(const uint8_t command, const uint8_t index, const uint8_t value) {
  if (index != 0) {
    return;  // Byte writes only
  }

  switch (command) {
    case xhd::write_config: {
      const uint16_t offset = (xhd_bank << 8) | xhd_index;
      if (offset < xhd::packet_size) {
        xhd_packet[offset] = value;
      }
      if (++xhd_index > 0xFF) {
        xhd_index = 0;
        xhd_bank++;
      }
      break;
    }

    case xhd::write_config_bank:
      xhd_bank = value;
      xhd_index = 0;
      break;

    case xhd::write_config_index:
      xhd_index = value;
      break;

    case xhd::write_config_apply:
      if (value == 0x01) {
        xhdPacketApplied();
      }
      break;

    default:
      break;
  }
}

void onWrite(const uint8_t address, const uint8_t reg, const uint8_t index, const uint8_t value) {
  if (address == xhd::address) {
    xhdWrite(reg, index, value);
    return;
  }

  encoder = address;
  quiet_ticks = 0;
  if (address == address_conexant) {
    conexantWrite(reg + index, value);
  } else if (address == address_focus) {
    focusWrite(reg + index, value);
  } else if (address == address_xcalibur) {
    xcaliburWrite(reg, index, value);
  }
}

// Conexant: RASTER_SEL (0x2E [1:0]) with HDTV_EN (bit 7) picks the raster. X-HD
// confirms 0x00 (SD interlaced) and 0xAD (480p, RASTER_SEL 1); 0xAA (720p,
// RASTER_SEL 2) was seen on a 720p console. 3 = 1080i follows the CX25871's
// RASTER_SEL order, not confirmed on an Xbox yet.
// SD width from H_ACTIVE (active width + 8): 728 = 720, 648 = 640, 704 = 640
// with 1.1:1 pixels.
static std::string conexantMode() {
  Mode mode;
  mode.source = "Conexant";
  if (cx_raster_valid) {
    mode.source += ", 0x2E = " + utils::toHex(cx_raster);
  }

  if (cx_hactive_valid) {
    mode.source += ", H_ACTIVE " + std::to_string(cx_hactive);
  }

  const bool    hdtv = (cx_raster & 0x80) != 0;
  const uint8_t raster = cx_raster & 0x03;
  if (!cx_raster_valid) {
    // Nothing yet
  } else if (!hdtv && raster == 0) {
    std::string width = "?";
    std::string par;

    if (cx_hactive_valid) {
      if (cx_hactive == 728) {
        width = "720";
      } else if (cx_hactive == 648) {
        width = "640";
      } else if (cx_hactive == 704) {
        width = "640";
        par = " (1.1:1 PAR)";
      }
    }

    std::string lines = "480/576";

    if (cx_wss_valid) {
      switch (cx_wss & 0x0F) {
        case 0x8:
          wssMode(mode, true, false);
          lines = "576";
          break;

        case 0x7:
          wssMode(mode, true, true);
          lines = "576";
          break;

        case 0x0:
          wssMode(mode, false, false);
          lines = "480";
          break;

        case 0x1:
          wssMode(mode, false, true);
          lines = "480";
          break;

        default:
          break;
      }
    }
    mode.resolution = width + "x" + lines + "i" + par;
  } else if (hdtv && raster == 1) {
    mode.resolution = "720x480p";  // Also the 640-wide 480p mode, in a 720-wide raster
  } else if (hdtv && raster == 2) {
    mode.resolution = "1280x720p";
    mode.aspect = "16:9";
  } else if (hdtv && raster == 3) {
    mode.resolution = "1080i";
    mode.aspect = "16:9";
  }

  if (cx_in_mode_have == 0x03) {
    const uint8_t m = cx_in_mode & 0x0F;
    mode.colour = (m == 0x0 || m == 0x1 || m == 0x2 || m == 0x8) ? "RGB" : "YCrCb";
  }
  return mode.text();
}

// Focus: VID_CNTL0 (0x92) says interlaced or progressive and RGB or YCrCb.
// In SD the width comes from IHO (103/143 = 720 wide, 151/115 = 640 with
// 1.1:1 pixels, other values 640), or from IHW (670 or more = 720) before
// IHO is seen. The Focus doesn't say which progressive mode (480p or 720p),
// X-HD measures that from the video signal.
static std::string focusMode() {
  Mode mode;
  mode.source = "Focus";
  if (fs_cntl_valid) {
    mode.source += ", 0x92 = " + utils::toHex(fs_cntl);
  }

  if (fs_ihw_valid) {
    mode.source += ", IHW " + std::to_string(fs_ihw);
  }

  if (fs_iho_valid) {
    mode.source += ", IHO " + std::to_string(fs_iho);
  }

  if (!fs_cntl_valid) {
    // Nothing yet
  } else if (fs_cntl & 0x80) {
    std::string width = "?";
    std::string par;

    if (fs_iho_valid) {
      width = (fs_iho == 103 || fs_iho == 143) ? "720" : "640";

      if (fs_iho == 115 || fs_iho == 151) {
        par = " (1.1:1 PAR)";
      }
    } else if (fs_ihw_valid) {
      width = fs_ihw >= 670 ? "720" : "640";
    }

    std::string lines = "480/576";

    if (fs_wss_valid) {
      if (fs_wss_b0 == 0x18 || fs_wss_b0 == 0x60) {
        wssMode(mode, false, fs_wss_b0 == 0x60);
        lines = "480";
      } else if (fs_wss_b0 == 0x00 && (fs_wss_b2 == 0x08 || fs_wss_b2 == 0x07)) {
        wssMode(mode, true, fs_wss_b2 == 0x07);
        lines = "576";
      }
    }

    mode.resolution = width + "x" + lines + "i" + par;
  } else {
    mode.resolution = "progressive (480p or 720p)";
  }

  if (fs_cntl_valid) {
    mode.colour = (fs_cntl & 0x04) ? "YCrCb" : "RGB";
  }

  return mode.text();
}

// Xcalibur: the whole mode, from its raster registers. The SD aspect is
// 0x07: 0x02/0x0E 16:9, 0x00/0x10 4:3 (0 is also written on 480p and HD).
static std::string xcaliburMode() {
  Mode mode;
  mode.source = "Xcalibur";
  const ModeInfo* info = modeInfo(xc_mode_index, true);
  if (info == nullptr) {
    mode.source += ", input raster " + std::to_string(xc_in_raster & 0x7FFF) + " lines";
    return mode.text();
  }

  mode.source += ", mode " + utils::toHex(xc_mode_index);
  mode.resolution = info->resolution;

  if (info->standard != nullptr) {
    mode.standard = info->standard;

    if (xc_aspect_valid) {
      if (xc_aspect == 0x02 || xc_aspect == 0x0E) {
        mode.aspect = "16:9";
      } else if (xc_aspect == 0x00 || xc_aspect == 0x10) {
        mode.aspect = "4:3";
      }
    }

  } else if (xc_mode_index >= 0x0A) {
    mode.aspect = "16:9";  // HD
  }
  return mode.text();
}

void update() {
  if (!changed) {
    return;
  }

  if (quiet_ticks < settle_ticks) {
    quiet_ticks++;
    return;
  }

  changed = false;

  std::string mode;
  if (encoder == address_conexant) {
    mode = conexantMode();
  } else if (encoder == address_focus) {
    mode = focusMode();
  } else if (encoder == address_xcalibur) {
    if (xc_mode_index == 0 && !xc_aspect_valid) {
      return;  // Nothing of a mode set yet
    }

    mode = xcaliburMode();

  }

  if (mode.empty() || mode == last_logged) {
    return;
  }

  last_logged = mode;
  debug::print_message("ENCODER: Video mode " + mode);
}

}  // namespace EncoderMode
}  // namespace SMC
