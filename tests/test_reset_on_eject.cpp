// Reset-on-eject (SMBus 0x19) across power cycles: like the PIC, every power on
// starts with it off and the first 0x19 write deciding

#include <gtest/gtest.h>

#include <cstdint>

#include "fakes.hpp"
#include "hal.hpp"
#include "smc.hpp"

namespace {

class ResetOnEjectTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    // As main_loop() at start-up
    SMC::globals_init();
    SMC::PLL_Reset::init();
  }

  // An SMBus write, then the main loop applying it
  static void write(uint8_t command, uint8_t data) {
    fake_i2c0.rx = command;
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
    fake_i2c0.rx = data;
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
    SMC::applySMBusWrites();
  }

  // The first tick of a power on: the PLL/reset state machine starts
  static void powerOn() {
    SMC::PLL_Reset::update();
  }

  static bool resetOnEjectOff() {
    return SMC::SMI::flags.reset_on_eject_disabled;
  }
  static bool writable() {
    return SMC::SMI::flags.reset_on_eject_writable;
  }
};

TEST_F(ResetOnEjectTest, OffAtFirstPowerOn) {
  powerOn();
  EXPECT_TRUE(resetOnEjectOff());
  EXPECT_TRUE(writable());
}

TEST_F(ResetOnEjectTest, FirstWriteDecides) {
  powerOn();
  write(0x19, 0x00);  // Enabled, e.g. a game from DVD
  EXPECT_FALSE(resetOnEjectOff());
  write(0x19, 0x01);  // Too late to turn it off again
  EXPECT_FALSE(resetOnEjectOff());
}

// The bug: after an Xbox power off the flags were cleared but the PLL/reset
// state machine wasn't restarted, so the next power on had reset-on-eject on
// and refused the kernel's 0x19 = 1. Opening the tray then reset the Xbox.
TEST_F(ResetOnEjectTest, OffAgainAfterPowerCycle) {
  powerOn();
  write(0x19, 0x01);
  EXPECT_FALSE(writable());

  SMC::resetAfterPowerOff();
  powerOn();
  EXPECT_TRUE(resetOnEjectOff());
  EXPECT_TRUE(writable());

  write(0x19, 0x01);
  EXPECT_TRUE(resetOnEjectOff());
}

}  // namespace
