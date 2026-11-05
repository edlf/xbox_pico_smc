// SMBus slave commands (src/smc_smbus.cpp), driven through
// handle_SMBus_interrupt() the way the Pico's I2C slave calls it

#include <gtest/gtest.h>

#include <cstdint>

#include "fakes.hpp"
#include "hal.hpp"
#include "smc.hpp"

namespace {

class SmbusTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::globals_init();
    SMC::Fan::init();
    SMC::Led::init();
  }

  // Xbox writes a byte: command byte, data byte, STOP. Only what the interrupt does.
  static void receive(uint8_t command, uint8_t data) {
    fake_i2c0.rx = command;
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
    fake_i2c0.rx = data;
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
  }

  // A write, then the main loop's next tick applying it
  static void write(uint8_t command, uint8_t data) {
    receive(command, data);
    SMC::applySMBusWrites();
  }

  // Xbox reads a byte: command byte, repeated START, read, STOP
  static uint8_t read(uint8_t command) {
    fake_i2c0.rx = command;
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_RECEIVE);
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_REQUEST);
    SMC::handle_SMBus_interrupt(i2c0, I2C_SLAVE_FINISH);
    return fake_i2c0.tx;
  }
};

TEST_F(SmbusTest, FirmwareVersionOneCharacterPerRead) {
  EXPECT_EQ(read(0x01), 'P');
  EXPECT_EQ(read(0x01), '1');
  EXPECT_EQ(read(0x01), '1');
  EXPECT_EQ(read(0x01), 'P');  // Wraps around
  read(0x01);
  write(0x01, 0x00);  // Restarts the string
  EXPECT_EQ(read(0x01), 'P');
}

// The reported version is a setting (USB config), the build's unless set
TEST_F(SmbusTest, FirmwareVersionFromTheSettings) {
  ASSERT_TRUE(SMC::Settings::setReportedVersion("P2L"));
  write(0x01, 0x00);
  EXPECT_EQ(read(0x01), 'P');
  EXPECT_EQ(read(0x01), '2');
  EXPECT_EQ(read(0x01), 'L');

  ASSERT_TRUE(SMC::Settings::setReportedVersion("auto"));
  EXPECT_EQ(read(0x01), SMC_VERSION_STRING[0]);
  EXPECT_EQ(read(0x01), SMC_VERSION_STRING[1]);
  EXPECT_EQ(read(0x01), SMC_VERSION_STRING[2]);
}

TEST_F(SmbusTest, ResetRequest) {
  write(0x02, 0x01);
  EXPECT_TRUE(SMC::PLL_Reset::flags.warm_reset_request);
  EXPECT_TRUE(SMC::SMI::flags.os_reset_request);
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
}

TEST_F(SmbusTest, PowerCycleRequest) {
  write(0x02, 0x40);
  EXPECT_TRUE(SMC::SMI::flags.power_cycle_request);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
}

TEST_F(SmbusTest, PowerOffRequest) {
  write(0x02, 0x80);
  EXPECT_TRUE(SMC::SMI::flags.power_off_request);
  EXPECT_TRUE(SMC::SMI::flags.power_change_requested);
}

TEST_F(SmbusTest, FanOverrideNeedsASpeedFirstCappedAt50) {
  write(0x05, 0x01);  // No speed set yet: refused
  EXPECT_FALSE(SMC::Fan::flags.fan_override);
  write(0x06, 70);
  EXPECT_EQ(SMC::Fan::getCustomFanSpeed(), 50);
  write(0x05, 0x01);
  EXPECT_TRUE(SMC::Fan::flags.fan_override);
  write(0x05, 0x00);
  EXPECT_FALSE(SMC::Fan::flags.fan_override);
}

TEST_F(SmbusTest, Temperatures) {
  SMC::Fan::sensors.CPU_temperature = 52;
  SMC::Fan::sensors.board_temperature = 38;
  EXPECT_EQ(read(0x09), 52);
  EXPECT_EQ(read(0x0A), 38);  // P11 reports the board sensor
}

TEST_F(SmbusTest, OsAnswerOnlyWhileWaitingForIt) {
  write(0x0D, 0x04);
  EXPECT_FALSE(SMC::SMI::flags.os_event_handled);  // Not waiting: ignored

  SMC::SMI::flags.waiting_for_os = true;
  write(0x0D, 0x02);  // Busy: still waiting
  EXPECT_TRUE(SMC::SMI::flags.os_busy);
  EXPECT_TRUE(SMC::SMI::flags.waiting_for_os);
  write(0x0D, 0x04);  // Handled
  EXPECT_TRUE(SMC::SMI::flags.os_event_handled);
  EXPECT_FALSE(SMC::SMI::flags.waiting_for_os);
}

TEST_F(SmbusTest, InterruptReasonIsClearedByReadingIt) {
  SMC::SMI::flags.interrupt_reason = 0x21;
  EXPECT_EQ(read(0x11), 0x21);
  EXPECT_EQ(read(0x11), 0x00);
}

// A retail board: chips 0-3 are 16 MB, 4-7 aren't fitted (so they "fail")
TEST_F(SmbusTest, RamTestPassed64Mb) {
  write(0x13, 0x0F);
  write(0x12, 0xF0);
  EXPECT_TRUE(SMC::BootChallenge::flags.ram_test_submitted);
  EXPECT_TRUE(SMC::BootChallenge::flags.ram_test_passed);
  EXPECT_EQ(read(0x14), 0xF0);
  EXPECT_EQ(read(0x15), 0x0F);
}

// A debug kit or an upgraded board: eight 16 MB chips
TEST_F(SmbusTest, RamTestPassed128Mb) {
  write(0x13, 0xFF);
  write(0x12, 0x00);
  EXPECT_TRUE(SMC::BootChallenge::flags.ram_test_passed);
}

// Eight 8 MB chips (early boards)
TEST_F(SmbusTest, RamTestPassed64MbOf8MbChips) {
  write(0x13, 0x00);
  write(0x12, 0x00);
  EXPECT_TRUE(SMC::BootChallenge::flags.ram_test_passed);
}

TEST_F(SmbusTest, RamTestFailed) {
  write(0x13, 0x0A);
  write(0x12, 0xF5);  // A 64 MB board, chips 0 and 2 dead
  EXPECT_TRUE(SMC::BootChallenge::flags.ram_test_submitted);
  EXPECT_FALSE(SMC::BootChallenge::flags.ram_test_passed);
}

TEST_F(SmbusTest, RamTestType0xFFAlsoNeedsAZeroResult) {
  write(0x13, 0xFF);  // P2L rule, for every firmware
  write(0x12, 0x01);
  EXPECT_FALSE(SMC::BootChallenge::flags.ram_test_passed);
}

// SMBus 0x13 and 0x12 decoded per chip
TEST_F(SmbusTest, RamTestDecoding) {
  using SMC::BootChallenge::decodeRamTest;
  SMC::BootChallenge::RamTest t = decodeRamTest(0xF0, 0x0F);  // Retail, passed
  EXPECT_EQ(t.fitted, 0x0F);
  EXPECT_EQ(t.failed, 0x00);
  EXPECT_EQ(t.wrong_size, 0x00);
  EXPECT_EQ(t.megabytes, 64);

  t = decodeRamTest(0xF1, 0x0E);  // Retail, chip 0 doesn't answer
  EXPECT_EQ(t.fitted, 0x0F);
  EXPECT_EQ(t.failed, 0x01);
  EXPECT_EQ(t.wrong_size, 0x00);

  t = decodeRamTest(0xF0, 0x0B);  // Retail, chip 2 answers but aliases at R11
  EXPECT_EQ(t.failed, 0x00);
  EXPECT_EQ(t.wrong_size, 0x04);

  t = decodeRamTest(0x00, 0xFF);  // 128 MB, passed
  EXPECT_EQ(t.fitted, 0xFF);
  EXPECT_EQ(t.megabytes, 128);
  EXPECT_EQ(t.board_mb, 128);
  EXPECT_EQ(t.found_mb, 128);

  t = decodeRamTest(0x20, 0xDF);  // 128 MB, chip 5 failed
  EXPECT_EQ(t.fitted, 0xFF);
  EXPECT_EQ(t.failed, 0x20);
  EXPECT_EQ(t.wrong_size, 0x00);
  EXPECT_EQ(t.megabytes, 64);  // The BIOS falls back to 64 MB

  t = decodeRamTest(0x00, 0x7F);  // 128 MB, chip 7 reads as 8 MB
  EXPECT_EQ(t.failed, 0x00);
  EXPECT_EQ(t.wrong_size, 0x80);

  t = decodeRamTest(0x08, 0x00);  // Eight 8 MB chips, chip 3 failed
  EXPECT_EQ(t.fitted, 0xFF);
  EXPECT_EQ(t.failed, 0x08);
  EXPECT_EQ(t.wrong_size, 0x00);  // No 16 MB chips to compare with
  EXPECT_EQ(t.board_mb, 64);
  EXPECT_EQ(t.chips_8mb, 7);
  EXPECT_EQ(t.found_mb, 56);

  t = decodeRamTest(0xF0, 0x0F);  // Retail, passed: 4 x 16 MB
  EXPECT_EQ(t.chips_16mb, 4);
  EXPECT_EQ(t.chips_8mb, 0);
  EXPECT_EQ(t.found_mb, 64);
  EXPECT_EQ(t.board_mb, 64);
}

TEST_F(SmbusTest, LastCommandAndByteWritten) {
  write(0x1B, 0x5A);
  EXPECT_EQ(read(0x16), 0x1B);
  EXPECT_EQ(read(0x17), 0x5A);
}

TEST_F(SmbusTest, ResetOnEjectOnlyTheFirstWriteCanKeepItOff) {
  SMC::SMI::flags.reset_on_eject_writable = true;  // As after a reset
  write(0x19, 0x01);
  EXPECT_TRUE(SMC::SMI::flags.reset_on_eject_disabled);
  write(0x19, 0x00);  // Enables it
  EXPECT_FALSE(SMC::SMI::flags.reset_on_eject_disabled);
  write(0x19, 0x01);  // Too late to disable it again
  EXPECT_FALSE(SMC::SMI::flags.reset_on_eject_disabled);
}

TEST_F(SmbusTest, OsReadyFollowsBit0) {
  write(0x1A, 0x01);
  EXPECT_TRUE(SMC::SMI::flags.os_ready);
  write(0x1A, 0x00);
  EXPECT_FALSE(SMC::SMI::flags.os_ready);
}

TEST_F(SmbusTest, ScratchRegister) {
  write(0x1B, 0xA5);
  EXPECT_EQ(read(0x1B), 0xA5);
}

TEST_F(SmbusTest, CustomSmcVersion) {
  EXPECT_EQ(read(0x30), 0x01);
  write(0x30, 0x55);  // Read-only
  EXPECT_EQ(read(0x30), 0x01);
}

TEST_F(SmbusTest, PicoTemperatureInWholeDegrees) {
  SMC::ExtraTemp::sensors.pico_temperature = 345;  // 34.5 °C
  EXPECT_EQ(read(0x31), 35);
  SMC::ExtraTemp::sensors.pico_temperature = -52;
  EXPECT_EQ(read(0x31), 0);
  SMC::ExtraTemp::sensors.pico_temperature = 3000;
  EXPECT_EQ(read(0x31), 0xFE);  // 0xFF means no reading
}

TEST_F(SmbusTest, NtcTemperatureIs0xFFWithoutOne) {
  SMC::ExtraTemp::sensors.ntc_connected = false;
  SMC::ExtraTemp::sensors.ntc_temperature = 251;
  EXPECT_EQ(read(0x32), 0xFF);
  SMC::ExtraTemp::sensors.ntc_connected = true;
  EXPECT_EQ(read(0x32), 25);
}

TEST_F(SmbusTest, LedBrightness) {
  hal::set_led_brightness(40);
  EXPECT_EQ(read(0x33), 40);

  write(0x34, 70);
  EXPECT_EQ(hal::get_led_brightness(), 70);
  EXPECT_TRUE(fake::logged("LED: Brightness 70% (SMBus 0x34)"));

  write(0x34, 0);  // Out of range: ignored
  write(0x34, 101);
  EXPECT_EQ(read(0x33), 70);
}

// The interrupt only queues writes that act on other modules, the main loop
// applies them at the start of its tick
TEST_F(SmbusTest, ModuleWritesWaitForTheMainLoop) {
  receive(0x0C, 0x00);  // Eject
  receive(0x07, 0x01);  // LED override
  EXPECT_FALSE(SMC::Dvd::flags.tray_eject_request);
  EXPECT_FALSE(SMC::Led::flags.led_override);
  SMC::applySMBusWrites();
  EXPECT_TRUE(SMC::Dvd::flags.tray_eject_request);
  EXPECT_TRUE(SMC::Led::flags.led_override);
}

// The slave's own registers change at once, so a read straight after sees them
TEST_F(SmbusTest, RegisterWritesReadBackAtOnce) {
  receive(0x1B, 0x42);
  EXPECT_EQ(read(0x1B), 0x42);
  receive(0x0E, 0x07);
  EXPECT_EQ(read(0x0F), 0x07);
  receive(0x13, 0x0F);
  receive(0x12, 0xF0);
  EXPECT_EQ(read(0x15), 0x0F);
  EXPECT_EQ(read(0x14), 0xF0);
  EXPECT_FALSE(SMC::BootChallenge::flags.ram_test_submitted);  // Its check waits for the main loop
  SMC::applySMBusWrites();
  EXPECT_TRUE(SMC::BootChallenge::flags.ram_test_passed);
}

// The kernel's boot flags: only changes are logged
TEST_F(SmbusTest, ScratchChangesAreLogged) {
  write(0x1B, 0x08);
  EXPECT_TRUE(fake::logged("BIOS: Scratch 0x00 -> 0x08 (SMBus 0x1B)"));
  fake::log.clear();
  write(0x1B, 0x08);
  EXPECT_FALSE(fake::logged("BIOS: Scratch"));
  EXPECT_EQ(read(0x1B), 0x08);
}

// Logged and kept for the web interface, also once 0x0F is cleared
TEST_F(SmbusTest, BiosErrorCodeIsLoggedAndKept) {
  uint8_t code;
  receive(0x0E, 0x10);
  SMC::applySMBusWrites();
  EXPECT_TRUE(fake::logged("BIOS: Error code 0x10 (16) written (SMBus 0x0E)"));
  ASSERT_TRUE(SMC::lastBiosErrorCode(code));
  EXPECT_EQ(code, 0x10);
  SMC::resetSMBusRegisters();  // Power off
  EXPECT_EQ(read(0x0F), 0x00);
  ASSERT_TRUE(SMC::lastBiosErrorCode(code));
  EXPECT_EQ(code, 0x10);
}

TEST_F(SmbusTest, QueuedWritesAreAppliedInOrder) {
  receive(0x20, 0x11);  // Challenge response, low byte first
  receive(0x21, 0x22);
  SMC::applySMBusWrites();
  EXPECT_EQ(SMC::BootChallenge::getChallengeStructRef().response0, 0x11);
  EXPECT_EQ(SMC::BootChallenge::getChallengeStructRef().response1, 0x22);
  EXPECT_TRUE(SMC::BootChallenge::flags.challenge_response_received);
}

// A power off request still queued when the Xbox goes off must not power it
// back on
TEST_F(SmbusTest, ResetDropsQueuedWrites) {
  receive(0x02, 0x80);
  SMC::resetSMBusRegisters();
  SMC::applySMBusWrites();
  EXPECT_FALSE(SMC::SMI::flags.power_change_requested);
}

TEST_F(SmbusTest, FullQueueIsReported) {
  for (int i = 0; i < 40; i++) {
    receive(0x07, 0x01);
  }
  SMC::applySMBusWrites();
  EXPECT_TRUE(fake::logged("SMBUS: 9 writes lost (queue full)"));  // 31 kept
}

}  // namespace
