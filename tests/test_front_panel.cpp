// Power and eject button debounce and interlock (src/smc_front_panel_sw.cpp)

#include <gtest/gtest.h>

#include "fakes.hpp"
#include "smc.hpp"

namespace {

class FrontPanelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::globals_init();
    SMC::FrontPanelSW::init();
  }

  static void ticks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::FrontPanelSW::update();
    }
  }
};

TEST_F(FrontPanelTest, EjectPressCountsAfterTwoReadings) {
  fake::hal.eject_button = true;
  ticks(1);
  EXPECT_FALSE(SMC::Dvd::flags.eject_change_requested);  // One reading isn't enough
  ticks(1);
  EXPECT_TRUE(SMC::Dvd::flags.eject_change_requested);
  EXPECT_TRUE(SMC::Dvd::flags.eject_button_event);
}

TEST_F(FrontPanelTest, HoldingAButtonCountsOnce) {
  fake::hal.power_button = true;
  ticks(2);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
  SMC::SMI::flags.power_change_requested = false;
  ticks(20);
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
}

TEST_F(FrontPanelTest, OneTickGlitchIsIgnored) {
  fake::hal.power_button = true;
  ticks(1);
  fake::hal.power_button = false;
  ticks(3);
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
}

TEST_F(FrontPanelTest, PowerAndEjectTogetherOnlyTheFirstCounts) {
  fake::hal.eject_button = true;
  ticks(2);
  EXPECT_TRUE(SMC::Dvd::flags.eject_change_requested);
  fake::hal.power_button = true;  // Eject still held
  ticks(3);
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
}

}  // namespace
