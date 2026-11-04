// Video mode decoding from the CPU's encoder writes (src/smc_encoder_mode.cpp)

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "fakes.hpp"
#include "smc_encoder_mode.hpp"

namespace {

constexpr uint8_t conexant = 0x45;
constexpr uint8_t focus = 0x6A;
constexpr uint8_t xcalibur = 0x70;
constexpr uint8_t xhd = 0x69;

class EncoderModeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::EncoderMode::reset();
  }

  // One register byte write, as the sniffer passes it on
  static void write(uint8_t address, uint8_t reg, uint8_t value) {
    SMC::EncoderMode::onWrite(address, reg, 0, value);
  }

  // Xcalibur block write: length byte, then the 32-bit value LSB first
  static void xcaliburBlock(uint8_t reg, uint32_t value) {
    SMC::EncoderMode::onWrite(xcalibur, reg, 0, 4);
    for (uint8_t i = 0; i < 4; i++) {
      SMC::EncoderMode::onWrite(xcalibur, reg, static_cast<uint8_t>(i + 1), static_cast<uint8_t>(value >> (8 * i)));
    }
  }

  // An Xcalibur mode set: input raster (htotal << 16 | SDTV << 15 | vtotal),
  // active width / 16 in the top byte of 0x52, output raster, 0x56 last
  static void xcaliburModeSet(uint16_t vtotal, bool sdtv, uint8_t width16, uint16_t out_vtotal, uint8_t mode_sel) {
    xcaliburBlock(0x50, (858u << 16) | (sdtv ? 0x8000u : 0u) | vtotal);
    xcaliburBlock(0x52, uint32_t{width16} << 24);
    xcaliburBlock(0x54, (858u << 16) | out_vtotal);
    xcaliburBlock(0x56, mode_sel);
  }

  // X-HD mode packet: bank 0, the 14 packet bytes, apply
  static void xhdPacket(const uint8_t (&packet)[14]) {
    write(xhd, 129, 0);
    for (uint8_t byte : packet) {
      write(xhd, 128, byte);
    }
    write(xhd, 131, 1);
  }

  // Ticks until the mode is logged (a mode set settles after 10 quiet ticks)
  static void settle() {
    for (int i = 0; i < 12; i++) {
      SMC::EncoderMode::update();
    }
  }

  static std::string lastLine() {
    return fake::log.empty() ? "" : fake::log.back();
  }
};

TEST_F(EncoderModeTest, Xcalibur480p720Wide) {
  xcaliburModeSet(0x20D, false, 0x2D, 0x20D, 0x60);
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 720x480p (Xcalibur, mode 0x08)");
}

TEST_F(EncoderModeTest, Xcalibur480i640WideNtsc4x3) {
  xcaliburModeSet(0x20D, true, 0x28, 0x20D, 0x60);
  xcaliburBlock(0x07, 0x00);
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 640x480i, NTSC, 4:3 (Xcalibur, mode 0x01)");
}

TEST_F(EncoderModeTest, Xcalibur576iPal16x9) {
  xcaliburModeSet(0x271, true, 0x2D, 0x271, 0x8A);
  xcaliburBlock(0x07, 0x0E);
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 720x576i, PAL, 16:9 (Xcalibur, mode 0x06)");
}

TEST_F(EncoderModeTest, XcaliburHdIs16x9) {
  xcaliburModeSet(0x2EE, false, 0x50, 0x2EE, 0x60);
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 1280x720p, 16:9 (Xcalibur, mode 0x0A)");

  xcaliburModeSet(0x465, false, 0x78, 0x465, 0x60);  // 1920 / 16
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 1920x1080i, 16:9 (Xcalibur, mode 0x0D)");

  xcaliburModeSet(0x465, false, 0x5A, 0x465, 0x60);  // 1440 / 16
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 1440x1080i, 16:9 (Xcalibur, mode 0x0C)");
}

TEST_F(EncoderModeTest, XcaliburUnknownRasterIsLogged) {
  xcaliburModeSet(0x123, false, 0x2D, 0x123, 0x60);
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode unknown resolution (Xcalibur, input raster 291 lines)");
}

TEST_F(EncoderModeTest, Conexant720pYCrCb) {
  write(conexant, 0x2E, 0xAA);  // HDTV_EN, RASTER_SEL 2
  write(conexant, 0xC6, 0x04);  // IN_MODE[2:0]
  write(conexant, 0x32, 0x08);  // IN_MODE[3]: 0b1100, YCrCb
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 1280x720p, 16:9, YCrCb (Conexant, 0x2E = 0xAA)");
}

TEST_F(EncoderModeTest, Conexant640x480iNtsc4x3Rgb) {
  write(conexant, 0x2E, 0x00);  // SD interlaced
  write(conexant, 0x78, 0x88);  // H_ACTIVE 0x288 = 648: 640 wide
  write(conexant, 0x60, 0xC0);  // WSS nibble 0: NTSC 4:3
  write(conexant, 0xC6, 0x00);
  write(conexant, 0x32, 0x00);  // IN_MODE 0: RGB
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 640x480i, NTSC, 4:3, RGB (Conexant, 0x2E = 0x00, H_ACTIVE 648)");
}

TEST_F(EncoderModeTest, ConexantMultiByteWriteIncrementsTheRegister) {
  write(conexant, 0x2E, 0x00);
  SMC::EncoderMode::onWrite(conexant, 0x5F, 1, 0xC7);  // Second byte after 0x5F: 0x60, PAL 16:9
  SMC::EncoderMode::onWrite(conexant, 0x77, 1, 0xD8);  // 0x78: H_ACTIVE 0x2D8 = 728, 720 wide
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 720x576i, PAL, 16:9 (Conexant, 0x2E = 0x00, H_ACTIVE 728)");
}

TEST_F(EncoderModeTest, ConexantProgressiveDropsTheSdWidthAndAspect) {
  write(conexant, 0x2E, 0x00);
  write(conexant, 0x78, 0x88);
  write(conexant, 0x60, 0xC0);
  settle();
  write(conexant, 0x2E, 0xAD);  // 480p
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 720x480p (Conexant, 0x2E = 0xAD)");
}

TEST_F(EncoderModeTest, Focus720x576iPal16x9YCrCb) {
  write(focus, 0x00, 103);   // IHO: 720 wide (PAL)
  write(focus, 0x83, 0x00);  // 625 lines: aspect in 0x85
  write(focus, 0x85, 0x07);  // 16:9
  write(focus, 0x92, 0x84);  // Interlaced, YCrCb
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 720x576i, PAL, 16:9, YCrCb (Focus, 0x92 = 0x84, IHO 103)");
}

TEST_F(EncoderModeTest, FocusProgressiveCantTell480pFrom720p) {
  write(focus, 0x92, 0x2E);
  settle();
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode progressive (480p or 720p), YCrCb (Focus, 0x92 = 0x2E)");
}

TEST_F(EncoderModeTest, LoggedOnceSettledAndOnlyWhenItChanges) {
  write(conexant, 0x2E, 0xAA);
  for (int i = 0; i < 9; i++) {
    SMC::EncoderMode::update();
  }
  EXPECT_TRUE(fake::log.empty());  // Still inside the mode set
  settle();
  EXPECT_EQ(fake::log.size(), 1u);

  write(conexant, 0x2E, 0xAA);  // Same mode written again
  settle();
  EXPECT_EQ(fake::log.size(), 1u);
}

TEST_F(EncoderModeTest, XhdModePacketGivesTheExactMode) {
  const uint8_t packet[14] = {0x8A, 0x01, 0x00, 0x00, 0x0D, 0x00};  // Conexant, mode dword 0x000D0000
  xhdPacket(packet);
  EXPECT_EQ(lastLine(), "(INFO ) ENCODER: Video mode 1920x1080i, 16:9 (X-HD mode packet, mode 0x0D, encoder 0x8A, mode dword 0x000D0000)");

  xhdPacket(packet);  // The dashboard repeats it: not logged again
  EXPECT_EQ(fake::log.size(), 1u);
}

}  // namespace
