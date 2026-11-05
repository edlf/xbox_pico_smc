// Whole-Xbox scenarios: the firmware's main loop in the simulator (tests/sim),
// with the PSU, drive and OS models around it

#include <gtest/gtest.h>

#include <memory>

#include "fakes.hpp"
#include "simulator.hpp"

namespace {

class ScenarioTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sim = std::make_unique<sim::Simulator>();
  }
  void TearDown() override {
    sim.reset();
  }

  // Power button, then until the dashboard runs
  void boot() {
    sim->pressPower();
    sim->run(3000);
    ASSERT_TRUE(sim->osRunning())
    << sim->status();
  }

  std::unique_ptr<sim::Simulator> sim;
};

TEST_F(ScenarioTest, PowerButtonBootsToTheDashboard) {
  boot();
  EXPECT_TRUE(sim->xboxOn());
  EXPECT_TRUE(fake::logged("BOOT: RAM test passed"));
  EXPECT_TRUE(fake::logged("BOOT: Challenge passed"));
  EXPECT_EQ(sim->resets(), 0);
}

TEST_F(ScenarioTest, PowerButtonShutsDownThroughTheOs) {
  boot();
  sim->pressPower();
  sim->run(3000);
  EXPECT_FALSE(sim->xboxOn()) << sim->status();
  EXPECT_TRUE(fake::logged("power off (0x0D = 0x01)"));
  EXPECT_FALSE(fake::logged("No OS answer"));
}

TEST_F(ScenarioTest, HungOsStillPowersOff) {
  boot();
  sim->os.answers = false;
  sim->pressPower();
  sim->run(3000);
  EXPECT_FALSE(sim->xboxOn()) << sim->status();
}

// "Busy" buys the OS 255 ticks (5.1 s), then the SMC powers off anyway
TEST_F(ScenarioTest, BusyOsDelaysPowerOff) {
  boot();
  sim->os.power_answer = sim::OsOptions::PowerAnswer::busy;
  sim->pressPower();
  sim->run(3000);
  EXPECT_TRUE(sim->xboxOn());
  sim->run(5000);
  EXPECT_FALSE(sim->xboxOn()) << sim->status();
}

TEST_F(ScenarioTest, EjectOpensAndClosesTheTray) {
  boot();
  sim->pressEject();
  sim->run(3000);
  EXPECT_EQ(sim->trayPins(), 0x10) << sim->status();
  sim->pressEject();
  sim->run(4000);
  EXPECT_EQ(sim->trayPins(), 0x60) << sim->status();
  EXPECT_EQ(sim->resets(), 0);
}

// The bug seen on hardware: after a power cycle, pressing eject repeatedly
// reset the Xbox (reset-on-eject left on, the kernel's 0x19 = 0x01 refused)
TEST_F(ScenarioTest, EjectStormAfterPowerCycleDoesNotReset) {
  boot();
  sim->pressPower();
  sim->run(4000);
  ASSERT_FALSE(sim->xboxOn());
  boot();

  for (int i = 0; i < 10; i++) {
    sim->pressEject();
    sim->run(150);
  }
  sim->run(5000);
  EXPECT_EQ(sim->resets(), 0) << sim->status();
  EXPECT_TRUE(sim->xboxOn());
  EXPECT_FALSE(fake::logged("reset on eject on"));
}

// A game from DVD turns reset-on-eject on: opening the tray then resets
TEST_F(ScenarioTest, ResetOnEjectResetsWhenEnabled) {
  boot();
  sim->smbusWrite(0x19, 0x00);
  sim->run(100);
  sim->pressEject();
  sim->run(3000);
  EXPECT_EQ(sim->resets(), 1) << sim->status();
  EXPECT_TRUE(fake::logged("Tray opened with reset on eject on"));
}

TEST_F(ScenarioTest, ResetRequestRebootsTheOs) {
  boot();
  sim->smbusWrite(0x02, 0x01);
  sim->run(3000);
  EXPECT_EQ(sim->resets(), 1) << sim->status();
  EXPECT_TRUE(sim->osRunning());
}

// Power cycle (0x02 bit 6): off for about 0.5 s, then back on
TEST_F(ScenarioTest, PowerCycleRequestComesBackOn) {
  boot();
  sim->smbusWrite(0x02, 0x40);
  sim->run(5000);
  EXPECT_EQ(sim->powerOns(), 2) << sim->status();
  EXPECT_TRUE(sim->osRunning());
}

// POWER_OK dropping while on: everything off, the Pico reboots by watchdog and
// the Xbox stays off (no power-on loop)
TEST_F(ScenarioTest, PsuDropoutPowersOffAndStaysOff) {
  boot();
  sim->psuDropout(200);
  sim->run(3000);
  EXPECT_TRUE(fake::logged("POWER_OK lost"));
  EXPECT_EQ(sim->watchdogReboots(), 1);
  sim->run(5000);
  EXPECT_FALSE(sim->xboxOn()) << sim->status();
  EXPECT_EQ(sim->powerOns(), 1);
}

}  // namespace
