// Fan speed changes (src/smc_fan.cpp): the ramp steps and the glide between
// speeds, driven through the shared functions both fan controls use

#include <gtest/gtest.h>

#include "fakes.hpp"
#include "smc.hpp"
#include "smc_fan_internal.hpp"

namespace {

class FanTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::globals_init();
    SMC::Fan::init();
  }

  // The ramp and the sensor bypass are settings, init() leaves them
  void TearDown() override {
    SMC::Fan::setRamp(SMC::Fan::default_ramp);
    SMC::Fan::setTemperatureBypass(false);
    SMC::ExtraTemp::setNtcConfig({SMC_NTC_SENSOR != 0, SMC_NTC_BETA, SMC_NTC_R25, SMC_NTC_SERIES_R});
    SMC::ExtraTemp::sensors = {};
  }

  // An NTC that's on and reads 52.6 °C, a Pico at 35.4 °C
  static void ntcReading() {
    SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
    ntc.enabled = true;
    ASSERT_TRUE(SMC::ExtraTemp::setNtcConfig(ntc));
    SMC::ExtraTemp::sensors.ntc_connected = true;
    SMC::ExtraTemp::sensors.ntc_temperature = 526;
    SMC::ExtraTemp::sensors.pico_temperature = 354;
  }

  static void ticks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::Fan::updateGlide();
    }
  }
};

TEST_F(FanTest, DefaultChangesAtOnceLikeThePic) {
  SMC::Fan::setSpeed(20);
  EXPECT_EQ(fake::hal.fan_speed, 20);
  SMC::Fan::setSpeed(35);
  EXPECT_EQ(fake::hal.fan_speed, 35);
  SMC::Fan::setSpeed(12);
  EXPECT_EQ(fake::hal.fan_speed, 12);
}

TEST_F(FanTest, DefaultStepsAreThePics) {
  SMC::Fan::setSpeed(20);
  EXPECT_EQ(SMC::Fan::rampedUp(1), 21);
  EXPECT_EQ(SMC::Fan::rampedUp(3), 23);
  EXPECT_EQ(SMC::Fan::rampedDown(), 19);
}

TEST_F(FanTest, StepsAreMultipliedAndLimited) {
  ASSERT_TRUE(SMC::Fan::setRamp({2, 4, 0, 0}));
  SMC::Fan::setSpeed(20);
  EXPECT_EQ(SMC::Fan::rampedUp(1), 22);
  EXPECT_EQ(SMC::Fan::rampedUp(3), 26);
  EXPECT_EQ(SMC::Fan::rampedDown(), 16);

  SMC::Fan::setSpeed(48);
  EXPECT_EQ(SMC::Fan::rampedUp(3), 50);  // Not past full speed
  SMC::Fan::setSpeed(12);
  EXPECT_EQ(SMC::Fan::rampedDown(), 10);  // Nor below the slowest
}

TEST_F(FanTest, RampValuesAreChecked) {
  EXPECT_FALSE(SMC::Fan::setRamp({0, 1, 0, 0}));
  EXPECT_FALSE(SMC::Fan::setRamp({1, 6, 0, 0}));
  EXPECT_FALSE(SMC::Fan::setRamp({1, 1, 1001, 0}));
  EXPECT_TRUE(SMC::Fan::setRamp({5, 5, 1000, 1000}));
}

// Glide up 100 ms per step: 5 ticks per speed step
TEST_F(FanTest, GlidesUpOneStepPerInterval) {
  ASSERT_TRUE(SMC::Fan::setRamp({1, 1, 100, 0}));
  SMC::Fan::setSpeed(20);  // The first speed after power on: at once
  EXPECT_EQ(fake::hal.fan_speed, 20);

  SMC::Fan::setSpeed(23);
  EXPECT_EQ(SMC::Fan::getFanSpeed(), 23);  // The target (SMBus 0x10)
  EXPECT_EQ(fake::hal.fan_speed, 20);      // Still on its way
  ticks(4);
  EXPECT_EQ(fake::hal.fan_speed, 20);
  ticks(1);
  EXPECT_EQ(fake::hal.fan_speed, 21);
  ticks(10);
  EXPECT_EQ(fake::hal.fan_speed, 23);
  ticks(20);
  EXPECT_EQ(fake::hal.fan_speed, 23);  // Stays there

  // No glide down: at once
  SMC::Fan::setSpeed(15);
  EXPECT_EQ(fake::hal.fan_speed, 15);
}

TEST_F(FanTest, GlidesDownSeparately) {
  ASSERT_TRUE(SMC::Fan::setRamp({1, 1, 0, 40}));  // 2 ticks per step down
  SMC::Fan::setSpeed(30);
  SMC::Fan::setSpeed(27);
  EXPECT_EQ(fake::hal.fan_speed, 30);
  ticks(2);
  EXPECT_EQ(fake::hal.fan_speed, 29);
  ticks(4);
  EXPECT_EQ(fake::hal.fan_speed, 27);
  SMC::Fan::setSpeed(40);  // No glide up: at once
  EXPECT_EQ(fake::hal.fan_speed, 40);
}

// An overheat goes straight to full speed, whatever the glide
TEST_F(FanTest, OverheatSkipsTheGlide) {
  ASSERT_TRUE(SMC::Fan::setRamp({1, 1, 1000, 1000}));
  SMC::Fan::setSpeed(20);
  SMC::Fan::setSpeedNow(50);
  EXPECT_EQ(fake::hal.fan_speed, 50);
  EXPECT_EQ(SMC::Fan::getOutputSpeed(), 50);
}

// Without the bypass: the ADM1032 at 0x4C, CPU register 0x01, board 0x00
TEST_F(FanTest, TemperaturesFromTheAdm1032) {
  ntcReading();
  ASSERT_TRUE(SMC::read_temperatures());
  EXPECT_EQ(SMC::Fan::sensors.CPU_temperature, 40);  // fake::hal's ADM1032
  EXPECT_EQ(SMC::Fan::sensors.board_temperature, 30);
  ASSERT_EQ(fake::hal.transfers.size(), 2u);
  EXPECT_EQ(fake::hal.transfers[0].address, 0x4C);
}

// Bypass: CPU from the NTC, board from the Pico, rounded, nothing on the bus
TEST_F(FanTest, BypassReadsTheNtcAndThePico) {
  SMC::Fan::setTemperatureBypass(true);
  ntcReading();
  ASSERT_TRUE(SMC::read_temperatures());
  EXPECT_EQ(SMC::Fan::sensors.CPU_temperature, 53);
  EXPECT_EQ(SMC::Fan::sensors.board_temperature, 35);
  EXPECT_FALSE(SMC::Fan::flags.temperature_read_failed);
  EXPECT_TRUE(fake::hal.transfers.empty());
}

// Bypass without an NTC reading: a failed read, like a dead ADM1032 (the fan
// control then forces an overheat)
TEST_F(FanTest, BypassWithoutTheNtcFails) {
  SMC::Fan::setTemperatureBypass(true);
  ntcReading();
  SMC::ExtraTemp::sensors.ntc_connected = false;  // Open or shorted
  EXPECT_FALSE(SMC::read_temperatures());
  EXPECT_TRUE(SMC::Fan::flags.temperature_read_failed);

  SMC::ExtraTemp::sensors.ntc_connected = true;
  SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
  ntc.enabled = false;  // Off in the settings
  ASSERT_TRUE(SMC::ExtraTemp::setNtcConfig(ntc));
  EXPECT_FALSE(SMC::read_temperatures());
  EXPECT_TRUE(fake::hal.transfers.empty());
}

// The P11 sensor configuration after reset isn't sent with the bypass
TEST_F(FanTest, BypassSkipsTheSensorConfiguration) {
  SMC::configureTemperatureSensor();
  EXPECT_EQ(fake::hal.transfers.size(), 1u);
  SMC::Fan::setTemperatureBypass(true);
  SMC::configureTemperatureSensor();
  EXPECT_EQ(fake::hal.transfers.size(), 1u);
}

}  // namespace
