#include "smc_encoder_mode.hpp"
#include "smc.hpp"
#include "text.hpp"

#include <cstdio>
#include <initializer_list>

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

// A mode as logged, e.g. "1280x720p, 16:9, YCrCb (Conexant, 0x2E = 0xAA)"
using ModeText = Text<160>;

static uint8_t  encoder = 0;      // Address of the encoder written to
static bool     changed = false;  // A mode register was written since the last log
static uint8_t  quiet_ticks = 0;  // Ticks since the last encoder write
static ModeText last_logged;

// What the CPU last wrote to each encoder's mode registers
struct ConexantRegs {
  bool     raster_valid = false;
  uint8_t  raster = 0;
  uint8_t  in_mode = 0;
  uint8_t  in_mode_have = 0;  // Bit 0: 0xC6 seen, bit 1: 0x32 seen
  uint16_t hactive = 0x0200;  // [10:8] is always 2, see X-HD
  bool     hactive_valid = false;
  uint8_t  wss = 0;
  bool     wss_valid = false;
};

struct FocusRegs {
  bool     cntl_valid = false;
  uint8_t  cntl = 0;
  uint16_t ihw = 720;  // Power-on default
  bool     ihw_valid = false;
  uint8_t  iho = 0;
  bool     iho_valid = false;
  uint8_t  wss_b0 = 0;
  uint8_t  wss_b2 = 0;
  bool     wss_valid = false;
};

struct XcaliburRegs {
  uint32_t value = 0;  // Block value being assembled
  uint32_t in_raster = 0;
  uint32_t width = 0;
  uint32_t out_raster = 0;
  uint8_t  mode_index = 0;    // 0: unknown raster
  bool     mode_set = false;  // A mode set completed (0x56 written)
  uint8_t  aspect = 0;
  bool     aspect_valid = false;
};

static ConexantRegs conexant;
static FocusRegs    focus;
static XcaliburRegs xcalibur;

static void resetXhd();

void reset() {
  resetXhd();
  encoder = 0;
  changed = false;
  quiet_ticks = 0;
  last_logged.clear();
  conexant = {};
  focus = {};
  xcalibur = {};
}

// The console's mode index (as in an X-HD mode packet), from the Xcalibur's
// raster registers. 0x0F, 0x10 and 0x11 are register-identical to 0x02, 0x04
// and 0x08 and read as those.
static uint8_t xcaliburModeIndex(const uint8_t mode_sel) {
  const uint16_t vtotal = xcalibur.in_raster & 0x7FFF;
  const bool     sdtv = (xcalibur.in_raster & 0x8000) != 0;
  const uint8_t  width16 = static_cast<uint8_t>(xcalibur.width >> 24);
  const bool     narrow = width16 == 0x28;  // 640

  switch (vtotal) {
    case 0x20D:  // 525 lines
      return sdtv ? (narrow ? 0x01 : 0x02) : (narrow ? 0x07 : 0x08);

    case 0x271:  // 625 lines
      if ((xcalibur.out_raster & 0x7FFF) == 0x208) {
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

static const ModeInfo* modeInfo(const uint8_t index, const bool is_xcalibur) {
  static constexpr ModeInfo xcalibur_480p_640 = {0x07, "640x480p", nullptr};
  if (index == 0x07 && is_xcalibur) {
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
  Text<40>    resolution;          // e.g. 720x480i
  const char* standard = nullptr;  // NTSC or PAL
  const char* aspect = nullptr;    // 4:3 or 16:9
  const char* colour = nullptr;    // RGB or YCrCb
  Text<96>    source;              // Encoder and raw values, to check the decode

  ModeText text() const {
    ModeText s(resolution.empty() ? "unknown resolution" : resolution.c_str());
    for (const char* part : {standard, aspect, colour}) {
      if (part != nullptr) {
        s.append(", %s", part);
      }
    }
    s.append(" (%s)", source.c_str());
    return s;
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
      conexant.raster = value;
      conexant.raster_valid = true;
      const bool interlaced = (value & 0x03) == 0 && (value & 0x80) == 0;
      if (!interlaced) {
        // Progressive mode sets don't rewrite the WSS or H_ACTIVE, drop the SD values
        conexant.wss_valid = false;
        conexant.hactive_valid = false;
      }

      break;
    }

    case cx::in_mode_lo:
      conexant.in_mode = (conexant.in_mode & 0x08) | (value & 0x07);
      conexant.in_mode_have |= 0x01;
      break;

    case cx::in_mode_hi:
      conexant.in_mode = (conexant.in_mode & 0x07) | (value & 0x08);
      conexant.in_mode_have |= 0x02;
      break;

    case cx::hactive_lo:
      conexant.hactive = (conexant.hactive & 0xFF00) | value;
      conexant.hactive_valid = true;
      break;

    case cx::hactive_hi:
      conexant.hactive = (conexant.hactive & 0x00FF) | (((value & 0x70) >> 4) << 8);
      break;

    case cx::wss_data:
      conexant.wss = value;
      conexant.wss_valid = true;
      break;

    default:
      return;
  }
  changed = true;
}

static void focusWrite(const uint8_t reg, const uint8_t value) {
  switch (reg) {
    case fs::vid_cntl0:
      focus.cntl = value;
      focus.cntl_valid = true;
      if (!(value & 0x80)) {
        focus.wss_valid = false;  // Progressive: no WSS, drop the SD values
        focus.iho_valid = false;
      }
      break;

    case fs::iho_lo:
      focus.iho = value;
      focus.iho_valid = true;
      break;

    case fs::ihw_lo:
      focus.ihw = (focus.ihw & 0xFF00) | value;
      focus.ihw_valid = true;
      break;

    case fs::ihw_hi:
      focus.ihw = (focus.ihw & 0x00FF) | ((value & 0x03) << 8);
      focus.ihw_valid = true;
      break;

    case fs::wss_b0:
      focus.wss_b0 = value;
      focus.wss_valid = true;
      break;

    case fs::wss_b2:
      focus.wss_b2 = value;
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
    xcalibur.value = 0;
  }

  xcalibur.value |= static_cast<uint32_t>(value) << (8 * (index - 1));
  if (index < 4) {
    return;
  }

  switch (reg) {
    case xc::in_raster:
      xcalibur.in_raster = xcalibur.value;
      break;

    case xc::width:
      xcalibur.width = xcalibur.value;
      break;

    case xc::out_raster:
      xcalibur.out_raster = xcalibur.value;
      break;

    case xc::mode_sel:
      xcalibur.mode_index = xcaliburModeIndex(xcalibur.value & 0xFF);
      xcalibur.mode_set = true;
      break;

    case xc::aspect:
      xcalibur.aspect = xcalibur.value & 0xFF;
      xcalibur.aspect_valid = true;
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

// Packet being written to the X-HD
struct XhdPacket {
  uint16_t bank = 0;
  uint16_t index = 0;
  uint8_t  bytes[xhd::packet_size] = {};
  uint32_t logged_mode = 0;  // Mode dword last logged
};
static XhdPacket xhd_packet;

static void xhdPacketApplied() {
  const uint8_t  encoder_address = xhd_packet.bytes[0];
  const uint32_t mode_dword =
      xhd_packet.bytes[2] | (xhd_packet.bytes[3] << 8) | (xhd_packet.bytes[4] << 16) | (static_cast<uint32_t>(xhd_packet.bytes[5]) << 24);

  if (mode_dword == xhd_packet.logged_mode) {
    return;  // The dashboard repeats it
  }

  xhd_packet.logged_mode = mode_dword;

  const uint8_t mode_index = (mode_dword >> 16) & 0xFF;
  const bool    is_xcalibur = encoder_address == (address_xcalibur << 1);

  Mode mode;
  mode.source.append("X-HD mode packet, mode 0x%02X, encoder 0x%02X, mode dword 0x%08lX", mode_index, encoder_address, static_cast<unsigned long>(mode_dword));
  const ModeInfo* info = modeInfo(mode_index, is_xcalibur);

  if (info != nullptr) {
    mode.resolution += info->resolution;
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

  debug::print(debug::Level::Info, "ENCODER: Video mode %s", mode.text().c_str());
}

static void resetXhd() {
  xhd_packet = {};
}

static void xhdWrite(const uint8_t command, const uint8_t index, const uint8_t value) {
  if (index != 0) {
    return;  // Byte writes only
  }

  switch (command) {
    case xhd::write_config: {
      const uint16_t offset = (xhd_packet.bank << 8) | xhd_packet.index;
      if (offset < xhd::packet_size) {
        xhd_packet.bytes[offset] = value;
      }
      if (++xhd_packet.index > 0xFF) {
        xhd_packet.index = 0;
        xhd_packet.bank++;
      }
      break;
    }

    case xhd::write_config_bank:
      xhd_packet.bank = value;
      xhd_packet.index = 0;
      break;

    case xhd::write_config_index:
      xhd_packet.index = value;
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
static ModeText conexantMode() {
  Mode mode;
  mode.source += "Conexant";
  if (conexant.raster_valid) {
    mode.source.append(", 0x2E = 0x%02X", conexant.raster);
  }

  if (conexant.hactive_valid) {
    mode.source.append(", H_ACTIVE %u", conexant.hactive);
  }

  const bool    hdtv = (conexant.raster & 0x80) != 0;
  const uint8_t raster = conexant.raster & 0x03;
  if (!conexant.raster_valid) {
    // Nothing yet
  } else if (!hdtv && raster == 0) {
    const char* width = "?";
    const char* par = "";

    if (conexant.hactive_valid) {
      if (conexant.hactive == 728) {
        width = "720";
      } else if (conexant.hactive == 648) {
        width = "640";
      } else if (conexant.hactive == 704) {
        width = "640";
        par = " (1.1:1 PAR)";
      }
    }

    const char* lines = "480/576";

    if (conexant.wss_valid) {
      switch (conexant.wss & 0x0F) {
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
    mode.resolution.append("%sx%si%s", width, lines, par);
  } else if (hdtv && raster == 1) {
    mode.resolution += "720x480p";  // Also the 640-wide 480p mode, in a 720-wide raster
  } else if (hdtv && raster == 2) {
    mode.resolution += "1280x720p";
    mode.aspect = "16:9";
  } else if (hdtv && raster == 3) {
    mode.resolution += "1080i";
    mode.aspect = "16:9";
  }

  if (conexant.in_mode_have == 0x03) {
    const uint8_t m = conexant.in_mode & 0x0F;
    mode.colour = (m == 0x0 || m == 0x1 || m == 0x2 || m == 0x8) ? "RGB" : "YCrCb";
  }
  return mode.text();
}

// Focus: VID_CNTL0 (0x92) says interlaced or progressive and RGB or YCrCb.
// In SD the width comes from IHO (103/143 = 720 wide, 151/115 = 640 with
// 1.1:1 pixels, other values 640), or from IHW (670 or more = 720) before
// IHO is seen. The Focus doesn't say which progressive mode (480p or 720p),
// X-HD measures that from the video signal.
static ModeText focusMode() {
  Mode mode;
  mode.source += "Focus";
  if (focus.cntl_valid) {
    mode.source.append(", 0x92 = 0x%02X", focus.cntl);
  }

  if (focus.ihw_valid) {
    mode.source.append(", IHW %u", focus.ihw);
  }

  if (focus.iho_valid) {
    mode.source.append(", IHO %u", focus.iho);
  }

  if (!focus.cntl_valid) {
    // Nothing yet
  } else if (focus.cntl & 0x80) {
    const char* width = "?";
    const char* par = "";

    if (focus.iho_valid) {
      width = (focus.iho == 103 || focus.iho == 143) ? "720" : "640";

      if (focus.iho == 115 || focus.iho == 151) {
        par = " (1.1:1 PAR)";
      }
    } else if (focus.ihw_valid) {
      width = focus.ihw >= 670 ? "720" : "640";
    }

    const char* lines = "480/576";

    if (focus.wss_valid) {
      if (focus.wss_b0 == 0x18 || focus.wss_b0 == 0x60) {
        wssMode(mode, false, focus.wss_b0 == 0x60);
        lines = "480";
      } else if (focus.wss_b0 == 0x00 && (focus.wss_b2 == 0x08 || focus.wss_b2 == 0x07)) {
        wssMode(mode, true, focus.wss_b2 == 0x07);
        lines = "576";
      }
    }

    mode.resolution.append("%sx%si%s", width, lines, par);
  } else {
    mode.resolution += "progressive (480p or 720p)";
  }

  if (focus.cntl_valid) {
    mode.colour = (focus.cntl & 0x04) ? "YCrCb" : "RGB";
  }

  return mode.text();
}

// Xcalibur: the whole mode, from its raster registers. The SD aspect is
// 0x07: 0x02/0x0E 16:9, 0x00/0x10 4:3 (0 is also written on 480p and HD).
static ModeText xcaliburMode() {
  Mode mode;
  mode.source += "Xcalibur";
  const ModeInfo* info = modeInfo(xcalibur.mode_index, true);
  if (info == nullptr) {
    mode.source.append(", input raster %lu lines", static_cast<unsigned long>(xcalibur.in_raster & 0x7FFF));
    return mode.text();
  }

  mode.source.append(", mode 0x%02X", xcalibur.mode_index);
  mode.resolution += info->resolution;

  if (info->standard != nullptr) {
    mode.standard = info->standard;

    if (xcalibur.aspect_valid) {
      if (xcalibur.aspect == 0x02 || xcalibur.aspect == 0x0E) {
        mode.aspect = "16:9";
      } else if (xcalibur.aspect == 0x00 || xcalibur.aspect == 0x10) {
        mode.aspect = "4:3";
      }
    }

  } else if (xcalibur.mode_index >= 0x0A) {
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

  ModeText mode;
  if (encoder == address_conexant) {
    mode = conexantMode();
  } else if (encoder == address_focus) {
    mode = focusMode();
  } else if (encoder == address_xcalibur) {
    if (!xcalibur.mode_set && !xcalibur.aspect_valid) {
      return;  // Nothing of a mode set yet
    }

    mode = xcaliburMode();
  }

  if (mode.empty() || mode == last_logged) {
    return;
  }

  last_logged = mode;
  debug::print(debug::Level::Info, "ENCODER: Video mode %s", mode.c_str());
}

}  // namespace EncoderMode
}  // namespace SMC
