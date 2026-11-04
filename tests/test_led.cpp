// Front panel LED patterns (src/smc_led.cpp): which pattern wins, and the
// power down fade

#include <gtest/gtest.h>

#include <cstdint>

#include "fakes.hpp"
#include "smc.hpp"

namespace {

// A pattern as shown: 4 phases per colour, bit 3 first
struct Shown {
  uint8_t red = 0;
  uint8_t green = 0;
};

bool operator==(const Shown& a, const Shown& b) {
  return a.red == b.red && a.green == b.green;
}

std::ostream& operator<<(std::ostream& os, const Shown& s) {
  return os << "red 0x" << std::hex << int{s.red} << " green 0x" << int{s.green};
}

class LedTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::globals_init();
    SMC::Led::init();
  }

  static void ticks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::Led::update();
    }
  }

  // One pattern cycle from its start: a phase every 9 ticks (pick, 6 ticks
  // shown, set), the first one tick later
  static Shown showPattern() {
    SMC::Led::resetPhaseCounter();
    Shown shown;
    for (int phase = 3; phase >= 0; phase--) {
      ticks(phase == 3 ? 10 : 9);
      if (fake::hal.red_led) {
        shown.red = static_cast<uint8_t>(shown.red | (1 << phase));
      }
      if (fake::hal.green_led) {
        shown.green = static_cast<uint8_t>(shown.green | (1 << phase));
      }
    }
    return shown;
  }

  static void powerOn() {
    SMC::Led::setPowerOff(false);
    ticks(60);  // Faded in
  }
};

TEST_F(LedTest, PoweredOffIsDark) {
  EXPECT_EQ(showPattern(), (Shown{0x0, 0x0}));
}

TEST_F(LedTest, PoweredOnIsSolidGreen) {
  powerOn();
  EXPECT_EQ(showPattern(), (Shown{0x0, 0xF}));
}

TEST_F(LedTest, TrayMovingBlinksGreen) {
  powerOn();
  SMC::Led::setQuickGreenBlink(true);
  EXPECT_EQ(showPattern(), (Shown{0x0, 0x5}));
}

TEST_F(LedTest, OverheatBeatsLedOverride) {
  powerOn();
  SMC::Led::setManualControl(true);
  SMC::Led::setRedPhases(0xF);
  SMC::Led::setOverheat(true);
  EXPECT_EQ(showPattern(), (Shown{0x3, 0x3}));
}

TEST_F(LedTest, AvMissingBeatsLedOverride) {
  powerOn();
  SMC::Led::setManualControl(true);
  SMC::Led::setRedPhases(0xA);
  SMC::Led::setGreenPhases(0x5);
  SMC::Led::setAvMissing(true);
  EXPECT_EQ(showPattern(), (Shown{0x5, 0xF}));
}

// P11/P2L: the SMBus override is shown even while the Xbox is off
TEST_F(LedTest, LedOverrideBeatsPowerOff) {
  SMC::Led::setManualControl(true);
  SMC::Led::setRedPhases(0xA);
  SMC::Led::setGreenPhases(0x0);
  EXPECT_EQ(showPattern(), (Shown{0xA, 0x0}));
}

TEST_F(LedTest, BootFailureBeatsEverything) {
  powerOn();
  SMC::Led::setOverheat(true);
  SMC::Led::setManualControl(true);
  SMC::Led::setBootFailurePattern(0x9, 0x6);
  SMC::Led::setBootFailure(true);
  EXPECT_EQ(showPattern(), (Shown{0x9, 0x6}));
}

TEST_F(LedTest, FadeOutKeepsPatternThenStaysDarkUntilOff) {
  powerOn();
  showPattern();
  SMC::Led::setPowerOff(true);
  EXPECT_TRUE(SMC::Led::isFadingOut());
  ticks(10);
  EXPECT_TRUE(fake::hal.green_led);  // The last pattern, fading
  ticks(30);
  EXPECT_FALSE(SMC::Led::isFadingOut());
  EXPECT_FALSE(fake::hal.green_led);

  // A pattern that wins over "off" stays dark until the Xbox is off
  SMC::Led::setManualControl(true);
  SMC::Led::setRedPhases(0xF);
  ticks(40);
  EXPECT_FALSE(fake::hal.red_led);
  EXPECT_EQ(showPattern(), (Shown{0xF, 0x0}));  // resetPhaseCounter(): off
}

}  // namespace
