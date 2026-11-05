// Whole-Xbox scenarios: the firmware's main loop in the simulator (tests/sim),
// with the PSU, drive and OS models around it

#include <gtest/gtest.h>

#include <memory>

#include "fakes.hpp"
#include "simulator.hpp"
#include "smc.hpp"

namespace {

class ScenarioTest : public ::testing::Test {
 protected:
  void SetUp() override {
    sim = std::make_unique<sim::Simulator>();
  }
  void TearDown() override {
    sim.reset();
    // Settings outlive a simulator run, as on the Pico
    SMC::Settings::resetToDefaults();
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

// Boot challenge not enforced (the default): a failed RAM test is only logged
TEST_F(ScenarioTest, RamTestFailureOnlyLoggedWhenNotEnforced) {
  SMC::BootChallenge::setEnforced(false);
  sim->os.ram_test_passes = false;
  boot();
  EXPECT_TRUE(fake::logged("BOOT: RAM test failed"));
  EXPECT_EQ(sim->resets(), 0);
}

// Enforced (the setting): the same failure resets the Xbox
TEST_F(ScenarioTest, RamTestFailureResetsWhenEnforced) {
  SMC::BootChallenge::setEnforced(true);
  sim->os.ram_test_passes = false;
  sim->pressPower();
  sim->run(6000);
  EXPECT_TRUE(fake::logged("BOOT: RAM test failed"));
  EXPECT_GT(sim->resets(), 0) << sim->status();
}

// AV pack setting: a fixed code on SMBus 0x04 whatever the cable (HDTV here)
TEST_F(ScenarioTest, FixedAvPackIsReported) {
  ASSERT_TRUE(SMC::Video::setAvPackOverride(4));  // Advanced AV
  boot();
  EXPECT_EQ(sim->smbusRead(0x04), 4);
  EXPECT_FALSE(SMC::Video::setAvPackOverride(7));
  EXPECT_FALSE(SMC::Video::setAvPackOverride(-2));
}

// DVD drive off (the setting): the tray reads as closed and empty, the disc in
// the drive ignored
TEST_F(ScenarioTest, NoDvdDriveReportsClosedAndEmpty) {
  SMC::Dvd::setSupported(false);
  boot();
  EXPECT_EQ(sim->smbusRead(0x03) & 0x70, 0x40);
}

// Sensor bypass with a working NTC: the CPU temperature (SMBus 0x09) is the
// NTC's (about 45 °C here) plus its offset, the ADM1032 (fake at 40 °C) isn't read
TEST_F(ScenarioTest, SensorBypassUsesTheNtc) {
  SMC::Fan::setTemperatureBypass(true);
  SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
  ntc.enabled = true;
  ASSERT_TRUE(SMC::ExtraTemp::setNtcConfig(ntc));
  fake::hal.ntc_adc = 1241;  // 10k B3950 with a 10k series resistor: ~45 °C
  boot();
  sim->run(3000);
  EXPECT_GE(sim->smbusRead(0x09), 54);  // Plus the default +10 offset
  EXPECT_LE(sim->smbusRead(0x09), 56);
  EXPECT_FALSE(fake::logged("forcing overheat"));
  EXPECT_TRUE(sim->osRunning());
}

// The NTC shutdown: the NTC (about 45 °C here) over a 40 °C limit shuts the
// Xbox down as an overheat would, while the CPU (fake ADM1032 at 40 °C) is fine
TEST_F(ScenarioTest, NtcOverTheShutdownLimitShutsDown) {
  SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
  ntc.enabled = true;
  ASSERT_TRUE(SMC::ExtraTemp::setNtcConfig(ntc));
  ASSERT_TRUE(SMC::ExtraTemp::setNtcShutdown({true, 40}));
  fake::hal.ntc_adc = 1241;
  sim->pressPower();  // The first fan decision comes before the dashboard
  sim->run(30000);
  EXPECT_TRUE(fake::logged("FAN: NTC at 4"));
  EXPECT_TRUE(fake::logged("its shutdown limit is 40 C"));
  EXPECT_TRUE(fake::logged("FAN: Overheated"));
  EXPECT_FALSE(sim->osRunning()) << sim->status();
}

// The same with the sensor bypass on: the NTC is the CPU's then, and 45 °C
// (plus the +10 offset) is far from the CPU's overheat threshold
TEST_F(ScenarioTest, NtcShutdownIsIgnoredWithTheSensorBypass) {
  SMC::Fan::setTemperatureBypass(true);
  SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
  ntc.enabled = true;
  ASSERT_TRUE(SMC::ExtraTemp::setNtcConfig(ntc));
  ASSERT_TRUE(SMC::ExtraTemp::setNtcShutdown({true, 40}));
  fake::hal.ntc_adc = 1241;
  boot();
  sim->run(30000);
  EXPECT_FALSE(fake::logged("FAN: NTC at"));
  EXPECT_FALSE(SMC::Fan::flags.overheated);
  EXPECT_TRUE(sim->osRunning()) << sim->status();
}

// Sensor bypass without the NTC: a dead sensor, so an overheat
TEST_F(ScenarioTest, SensorBypassWithoutTheNtcForcesAnOverheat) {
  SMC::Fan::setTemperatureBypass(true);
  sim->pressPower();
  sim->run(4000);
  EXPECT_TRUE(fake::logged("FAN: No NTC reading (sensor bypass), forcing overheat"));
}

// Power button off: it doesn't start the Xbox, BOOTSEL does, and shuts it
// down through the OS like the power button
TEST_F(ScenarioTest, BootselIsThePowerButtonWhenItsOff) {
  SMC::FrontPanelSW::setPowerButtonEnabled(false);
  sim->pressPower();
  sim->run(3000);
  EXPECT_FALSE(sim->xboxOn());

  sim->pressBootsel();
  sim->run(3000);
  ASSERT_TRUE(sim->osRunning())
  << sim->status();
  sim->pressBootsel();
  sim->run(3000);
  EXPECT_FALSE(sim->xboxOn()) << sim->status();
}

// Eject button off: the tray stays where it is
TEST_F(ScenarioTest, EjectButtonOffLeavesTheTray) {
  SMC::FrontPanelSW::setEjectButtonEnabled(false);
  boot();
  sim->pressEject();
  sim->run(3000);
  EXPECT_EQ(sim->smbusRead(0x03) & 0x70, 0x60);  // Still closed, with the disc
}

// The UART console's commands: the power button from standby and while
// running (through the OS), the eject button
TEST_F(ScenarioTest, SerialConsolePowerAndEject) {
  fake::hal.serial_input = "power\r\n";
  sim->run(3000);
  ASSERT_TRUE(sim->osRunning())
  << sim->status();
  EXPECT_TRUE(fake::logged("SERIAL: Power button"));

  fake::hal.serial_input = "eject\n";
  sim->run(3000);
  EXPECT_EQ(sim->smbusRead(0x03) & 0x70, 0x10);  // Open

  fake::hal.serial_input = "power\n";
  sim->run(3000);
  EXPECT_FALSE(sim->xboxOn()) << sim->status();
}

// Even with the front panel buttons turned off in the settings
TEST_F(ScenarioTest, SerialConsoleWorksWithTheButtonsOff) {
  SMC::FrontPanelSW::setPowerButtonEnabled(false);
  SMC::FrontPanelSW::setEjectButtonEnabled(false);
  fake::hal.serial_input = "power\n";
  sim->run(3000);
  EXPECT_TRUE(sim->osRunning()) << sim->status();
}

TEST_F(ScenarioTest, SerialConsoleRefusesOtherLines) {
  fake::hal.serial_input = "help\nreboot\npowerpowerpowerpower\n\n";
  sim->run(200);
  EXPECT_TRUE(fake::logged("SERIAL: Commands: power (power button), eject (eject button)"));
  EXPECT_TRUE(fake::logged("SERIAL: Unknown command 'reboot', try help"));
  EXPECT_TRUE(fake::logged("SERIAL: Line too long, ignored"));
  EXPECT_FALSE(fake::logged("SERIAL: Unknown command 'powerpowerpower'"));
  EXPECT_FALSE(sim->xboxOn());
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
