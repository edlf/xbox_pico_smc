// SMBus decoder behind the sniffer (src/smc_i2c_decoder.cpp), fed SDA/SCL
// levels the way the PIO captures them

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <string>

#include "fakes.hpp"
#include "smc_i2c_decoder.hpp"

namespace {

constexpr uint8_t conexant = 0x45;
constexpr uint8_t focus = 0x6A;
constexpr uint8_t temperature_sensor = 0x4C;

class I2CDecoderTest : public ::testing::Test {
 protected:
  void SetUp() override {
    SMC::I2CDecoder::setIgnoring(false);
    SMC::I2CDecoder::resetFound();
    report(100);  // Nothing left over from the last test
    fake::reset();
  }

  static void report(int ticks = 1) {
    for (int i = 0; i < ticks; i++) {
      SMC::I2CDecoder::report();
    }
  }

  static void level(bool sda, bool scl) {
    SMC::I2CDecoder::sample(static_cast<uint8_t>((sda ? 1 : 0) | (scl ? 2 : 0)));
  }

  // SDA falls while SCL is high, then SCL low
  static void start() {
    level(true, true);
    level(false, true);
    level(false, false);
  }

  // SCL low: SDA released, SCL high, then SDA falls
  static void repeatedStart() {
    level(true, false);
    level(true, true);
    level(false, true);
    level(false, false);
  }

  // SDA rises while SCL is high
  static void stop() {
    level(false, false);
    level(false, true);
    level(true, true);
  }

  // SDA set while SCL is low, read on the rising edge
  static void bit(bool value) {
    level(value, false);
    level(value, true);
    level(value, false);
  }

  // 8 bits MSB first, then the ACK clock (SDA low: acknowledged)
  static void byte(uint8_t value, bool ack = true) {
    for (int i = 7; i >= 0; i--) {
      bit(((value >> i) & 1) != 0);
    }
    bit(!ack);
  }

  // The CPU writes data to reg of a device
  static void write(uint8_t address, uint8_t reg, std::initializer_list<uint8_t> data, bool ack = true) {
    start();
    byte(static_cast<uint8_t>(address << 1));
    byte(reg);
    for (uint8_t d : data) {
      byte(d, ack);
    }
    stop();
  }

  static int countLogged(const std::string& text) {
    int count = 0;
    for (const std::string& line : fake::log) {
      count += line.find(text) != std::string::npos ? 1 : 0;
    }
    return count;
  }
};

TEST_F(I2CDecoderTest, EncoderThatAnswersIsFoundOnce) {
  write(conexant, 0x2E, {0x00});
  report();
  EXPECT_EQ(countLogged("(INFO ) ENCODER: Found Conexant (0x45)"), 1);
  write(conexant, 0x2E, {0x00});
  report();
  EXPECT_EQ(countLogged("ENCODER: Found"), 1);
}

TEST_F(I2CDecoderTest, EncoderThatDoesNotAnswerIsNotFound) {
  start();
  byte(static_cast<uint8_t>(focus << 1), false);  // The BIOS probing an encoder this board doesn't have
  stop();
  report();
  EXPECT_FALSE(fake::logged("ENCODER: Found"));
}

TEST_F(I2CDecoderTest, FoundAgainAfterReset) {
  write(conexant, 0x2E, {0x00});
  report();
  SMC::I2CDecoder::resetFound();
  write(conexant, 0x2E, {0x00});
  report();
  EXPECT_EQ(countLogged("ENCODER: Found Conexant"), 2);
}

TEST_F(I2CDecoderTest, EncoderWriteIsLogged) {
  write(conexant, 0x2E, {0xAA});
  report();
  EXPECT_TRUE(fake::logged("(DEBUG) ENCODER: Conexant [0x2E] = 0xAA"));
}

TEST_F(I2CDecoderTest, LongWriteKeepsFourBytes) {
  write(conexant, 0x10, {0x01, 0x02, 0x03, 0x04, 0x05, 0x06});
  report();
  EXPECT_TRUE(fake::logged("ENCODER: Conexant [0x10] = 0x01 0x02 0x03 0x04 (+2 bytes)"));
}

TEST_F(I2CDecoderTest, NackedWriteIsMarked) {
  write(conexant, 0x2E, {0xAA}, false);
  report();
  EXPECT_TRUE(fake::logged("ENCODER: Conexant [0x2E] = 0xAA (NACK)"));
}

TEST_F(I2CDecoderTest, ReadIsNotLogged) {
  start();
  byte(static_cast<uint8_t>(conexant << 1));
  byte(0x2E);
  repeatedStart();
  byte(static_cast<uint8_t>((conexant << 1) | 1));
  byte(0xAA, false);  // The CPU doesn't acknowledge the last byte it reads
  stop();
  report();
  EXPECT_TRUE(fake::logged("ENCODER: Found Conexant"));
  EXPECT_FALSE(fake::logged("ENCODER: Conexant ["));
}

TEST_F(I2CDecoderTest, OtherDevicesAreNotLogged) {
  write(temperature_sensor, 0x01, {0x40});
  report();
  EXPECT_TRUE(fake::log.empty());
}

TEST_F(I2CDecoderTest, OwnTransfersAreIgnored) {
  SMC::I2CDecoder::setIgnoring(true);
  write(conexant, 0x2E, {0xAA});
  SMC::I2CDecoder::setIgnoring(false);
  report();
  EXPECT_TRUE(fake::log.empty());
}

TEST_F(I2CDecoderTest, WritesTwoPerTick) {
  for (uint8_t reg = 0; reg < 5; reg++) {
    write(conexant, reg, {0x00});
  }
  report();
  EXPECT_EQ(countLogged("ENCODER: Conexant ["), 2);
  report(2);
  EXPECT_EQ(countLogged("ENCODER: Conexant ["), 5);
}

// The decoded writes reach EncoderMode, which logs the video mode
TEST_F(I2CDecoderTest, VideoModeFromBusWrites) {
  write(conexant, 0x2E, {0xAA});  // HDTV_EN, RASTER_SEL 2
  write(conexant, 0xC6, {0x04});  // IN_MODE[2:0]
  write(conexant, 0x32, {0x08});  // IN_MODE[3]: YCrCb
  report(12);
  EXPECT_TRUE(fake::logged("(INFO ) ENCODER: Video mode 1280x720p, 16:9, YCrCb (Conexant, 0x2E = 0xAA)"));
}

}  // namespace
