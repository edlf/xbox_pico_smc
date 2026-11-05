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

  // The buttons' settings outlive init(), as on the Pico
  void TearDown() override {
    SMC::FrontPanelSW::setPowerButtonEnabled(true);
    SMC::FrontPanelSW::setEjectButtonEnabled(true);
    SMC::FrontPanelSW::setBootselPower(SMC_BOOTSEL_POWER);
  }

  static void ticks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::FrontPanelSW::update();
    }
  }
};

// The UART console's and web interface's presses: only while the other button
// is idle, as a front panel press
TEST_F(FrontPanelTest, RemotePressesWaitForTheOtherButton) {
  fake::hal.eject_button = true;
  ticks(2);
  SMC::Dvd::flags.eject_change_requested = false;
  EXPECT_FALSE(SMC::FrontPanelSW::pressPower());
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
  fake::hal.eject_button = false;
  ticks(2);
  EXPECT_TRUE(SMC::FrontPanelSW::pressPower());
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
  EXPECT_TRUE(SMC::FrontPanelSW::pressEject());
  EXPECT_TRUE(SMC::Dvd::flags.eject_change_requested);
}

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

// BOOTSEL as a power button off: it does nothing
TEST_F(FrontPanelTest, BootselUnusedWhenOff) {
  SMC::FrontPanelSW::setBootselPower(false);
  fake::hal.bootsel = true;
  ticks(5);
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
  EXPECT_FALSE(SMC::FrontPanelSW::bootselIsPower());
}

// Power button off: it's ignored, and BOOTSEL powers the Xbox instead
TEST_F(FrontPanelTest, PowerButtonOffMakesBootselThePowerButton) {
  SMC::FrontPanelSW::setBootselPower(false);
  SMC::FrontPanelSW::setPowerButtonEnabled(false);
  EXPECT_TRUE(SMC::FrontPanelSW::bootselIsPower());  // Even though it's not set
  fake::hal.power_button = true;
  ticks(5);
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
  fake::hal.power_button = false;
  ticks(2);

  fake::hal.bootsel = true;
  ticks(2);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
}

// BOOTSEL as well as the power button: either one works
TEST_F(FrontPanelTest, BootselAsWellAsThePowerButton) {
  SMC::FrontPanelSW::setBootselPower(true);
  fake::hal.bootsel = true;
  ticks(2);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
  SMC::SMI::flags.power_change_requested = false;
  fake::hal.bootsel = false;
  ticks(2);
  fake::hal.power_button = true;
  ticks(2);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
}

TEST_F(FrontPanelTest, EjectButtonOffIsIgnored) {
  SMC::FrontPanelSW::setEjectButtonEnabled(false);
  fake::hal.eject_button = true;
  ticks(5);
  EXPECT_FALSE(SMC::Dvd::flags.eject_change_requested);
  // And doesn't block the power button (the interlock)
  fake::hal.power_button = true;
  ticks(2);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
}

}  // namespace
