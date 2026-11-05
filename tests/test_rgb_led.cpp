// RGB LED (src/smc_rgb_led.cpp): modes, brightness and the SMBus colour,
// through a whole boot in the simulator

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "fakes.hpp"
#include "simulator.hpp"
#include "smc.hpp"

namespace {

using Colour = std::array<uint8_t, 3>;

class RgbLedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sim = std::make_unique<sim::Simulator>();
  }

  void TearDown() override {
    sim.reset();
    // The settings outlive a simulator run, as on the Pico
    SMC::RgbLed::setMode(static_cast<SMC::RgbLed::Mode>(SMC_RGB_MODE));
    SMC::RgbLed::setBrightness(SMC_RGB_BRIGHTNESS);
  }

  static Colour colour() {
    return {fake::hal.rgb[0], fake::hal.rgb[1], fake::hal.rgb[2]};
  }

  // Power button, then until the dashboard runs (still in the 5 s of green)
  void boot() {
    sim->pressPower();
    sim->run(3000);
    ASSERT_TRUE(sim->osRunning())
    << sim->status();
  }

  std::unique_ptr<sim::Simulator> sim;
};

constexpr Colour dark = {0, 0, 0};
constexpr Colour green = {0, 255, 0};

TEST_F(RgbLedTest, BootModeIsGreenThenDark) {
  EXPECT_EQ(colour(), dark);  // Xbox off
  boot();
  EXPECT_EQ(colour(), green);
  sim->run(6000);
  EXPECT_EQ(colour(), dark);
}

// Every colour shown during a boot, in order
TEST_F(RgbLedTest, BootPhasesInOrder) {
  std::vector<Colour> seen = {colour()};
  sim->pressPower();
  for (int tick = 0; tick < 150; tick++) {
    if (colour() != seen.back()) {
      seen.push_back(colour());
    }
    sim->run(20);
  }
  // clang-format off: one colour per line, it would pack them into a table
  const std::vector<Colour> expected = {
      dark,
      {255, 0, 255},  // Powered, held in reset
      {0, 0, 255},    // BIOS
      {0, 255, 255},  // RAM test passed, kernel
      green,          // Booted
  };
  // clang-format on
  EXPECT_EQ(seen, expected);
}

TEST_F(RgbLedTest, MirrorModeFollowsTheFrontPanelAfterBooting) {
  SMC::RgbLed::setMode(SMC::RgbLed::Mode::mirror);
  boot();
  sim->run(6000);
  EXPECT_EQ(colour(), green);  // Solid green front panel

  sim->smbusWrite(0x08, 0xF0);  // Front panel red, through the SMBus override
  sim->smbusWrite(0x07, 0x01);
  sim->run(1000);
  EXPECT_EQ(colour(), (Colour{255, 0, 0}));

  sim->smbusWrite(0x08, 0xFF);  // Both: orange
  sim->run(1000);
  EXPECT_EQ(colour(), (Colour{255, 80, 0}));
}

TEST_F(RgbLedTest, OffModeStaysDarkEvenWhileBooting) {
  SMC::RgbLed::setMode(SMC::RgbLed::Mode::off);
  sim->pressPower();
  EXPECT_EQ(colour(), dark);
  sim->run(2900);
  EXPECT_EQ(colour(), dark);
  sim->smbusWrite(0x38, 0xFF);  // Not even an SMBus colour
  sim->smbusWrite(0x37, 0x01);
  sim->run(100);
  EXPECT_EQ(colour(), dark);
}

TEST_F(RgbLedTest, BrightnessScalesTheColour) {
  sim->smbusWrite(0x36, 50);
  boot();
  EXPECT_EQ(colour(), (Colour{0, 127, 0}));
  EXPECT_EQ(sim->smbusRead(0x35), 50);
  EXPECT_TRUE(fake::logged("RGB: Brightness 50% (SMBus 0x36)"));
}

TEST_F(RgbLedTest, BrightnessOutOfRangeIsIgnored) {
  sim->smbusWrite(0x36, 0);
  sim->smbusWrite(0x36, 101);
  sim->run(100);
  EXPECT_EQ(sim->smbusRead(0x35), SMC_RGB_BRIGHTNESS);
}

TEST_F(RgbLedTest, SmbusColourUntilAutoOrReset) {
  boot();
  sim->smbusWrite(0x38, 0x12);
  sim->smbusWrite(0x39, 0x34);
  sim->smbusWrite(0x3A, 0x56);
  sim->smbusWrite(0x37, 0x01);
  sim->run(100);
  EXPECT_EQ(colour(), (Colour{0x12, 0x34, 0x56}));
  EXPECT_EQ(sim->smbusRead(0x37), 0x01);
  EXPECT_EQ(sim->smbusRead(0x39), 0x34);

  sim->smbusWrite(0x37, 0x00);  // Back to automatic
  sim->run(100);
  EXPECT_EQ(colour(), green);

  sim->smbusWrite(0x37, 0x01);
  sim->smbusWrite(0x02, 0x01);  // Reset: automatic again
  sim->run(3000);
  EXPECT_EQ(sim->smbusRead(0x37), 0x00);
  EXPECT_EQ(colour(), green);
}

}  // namespace
