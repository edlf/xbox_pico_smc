// Settings saved in flash (src/smc_settings.cpp), on the fake flash

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "config.hpp"
#include "fakes.hpp"
#include "hal.hpp"
#include "hardware/flash.h"
#include "smc.hpp"
#include "smc_settings.hpp"

namespace {

constexpr uint32_t record_offset = PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

class SettingsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    fake::eraseFlash();
    SMC::Fan::init();
    SMC::Fan::resetThresholds();
    SMC::Settings::resetWifi();
    SMC::Settings::setWebEnabled(true);
  }

  // The RGB settings live in the module, back to the defaults for the next test
  void TearDown() override {
    SMC::RgbLed::setMode(static_cast<SMC::RgbLed::Mode>(SMC_RGB_MODE));
    SMC::RgbLed::setBrightness(SMC_RGB_BRIGHTNESS);
  }

  static uint8_t* record() {
    return fake::flash() + record_offset;
  }

  // The record's size and checksum (FNV-1a over all of it but the last 4 bytes)
  static uint16_t recordSize() {
    return static_cast<uint16_t>(record()[6] | (record()[7] << 8));
  }

  static void fixChecksum() {
    const uint16_t size = recordSize();
    uint32_t       hash = 2166136261u;
    for (uint16_t i = 0; i < size - 4; i++) {
      hash = (hash ^ record()[i]) * 16777619u;
    }
    std::memcpy(record() + size - 4, &hash, sizeof(hash));
  }

  // Settings that differ from the defaults in every field
  static void changeEverything() {
    hal::set_led_brightness(40);
    ASSERT_TRUE(SMC::Fan::setThreshold(0, static_cast<uint8_t>(SMC::Fan::getThreshold(0) - 1)));
    ASSERT_TRUE(SMC::Settings::setWifiSsid("Living room"));
    ASSERT_TRUE(SMC::Settings::setWifiPassword("correct horse"));
    SMC::Settings::setWebEnabled(false);
    SMC::RgbLed::setMode(SMC::RgbLed::Mode::mirror);
    ASSERT_TRUE(SMC::RgbLed::setBrightness(30));
  }

  static void backToDefaults() {
    SMC::RgbLed::setMode(static_cast<SMC::RgbLed::Mode>(SMC_RGB_MODE));
    SMC::RgbLed::setBrightness(SMC_RGB_BRIGHTNESS);
    hal::set_led_brightness(100);
    SMC::Fan::resetThresholds();
    SMC::Settings::resetWifi();
    SMC::Settings::setWebEnabled(true);
  }
};

TEST_F(SettingsTest, NoneSavedKeepsDefaults) {
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged("SETTINGS: None saved, using the defaults"));
  EXPECT_EQ(hal::get_led_brightness(), 100);
  EXPECT_STREQ(SMC::Settings::wifiSsid(), SMC_WIFI_SSID);
  EXPECT_TRUE(SMC::Settings::webEnabled());
}

TEST_F(SettingsTest, SavedSettingsLoadBack) {
  SMC::Settings::load();
  changeEverything();
  const uint8_t threshold = SMC::Fan::getThreshold(0);
  ASSERT_TRUE(SMC::Settings::saveNow());
  EXPECT_TRUE(fake::logged("SETTINGS: Saved"));

  backToDefaults();
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged("SETTINGS: Loaded, LED brightness 40%, fan thresholds, Wi-Fi name, Wi-Fi password, web interface off, RGB LED mirror 30%"));
  EXPECT_EQ(hal::get_led_brightness(), 40);
  EXPECT_EQ(SMC::Fan::getThreshold(0), threshold);
  EXPECT_STREQ(SMC::Settings::wifiSsid(), "Living room");
  EXPECT_STREQ(SMC::Settings::wifiPassword(), "correct horse");
  EXPECT_FALSE(SMC::Settings::webEnabled());
  EXPECT_EQ(SMC::RgbLed::mode(), SMC::RgbLed::Mode::mirror);
  EXPECT_EQ(SMC::RgbLed::brightness(), 30);
}

TEST_F(SettingsTest, UnchangedSettingsAreNotWritten) {
  SMC::Settings::load();
  EXPECT_TRUE(SMC::Settings::saveNow());
  EXPECT_TRUE(fake::logged("SETTINGS: Nothing changed, not saved"));
  EXPECT_EQ(fake::flash_writes, 0);
}

// A name changed and changed back is the same record: nothing left over from
// the longer one
TEST_F(SettingsTest, ShorterNameChangedBackIsUnchanged) {
  SMC::Settings::load();
  ASSERT_TRUE(SMC::Settings::setWifiSsid("A much longer network name"));
  SMC::Settings::resetWifi();
  EXPECT_TRUE(SMC::Settings::saveNow());
  EXPECT_EQ(fake::flash_writes, 0);
}

TEST_F(SettingsTest, CorruptRecordIsIgnored) {
  SMC::Settings::load();
  changeEverything();
  ASSERT_TRUE(SMC::Settings::saveNow());
  record()[12] ^= 0x01;  // LED brightness

  hal::set_led_brightness(70);
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged("SETTINGS: None saved, using the defaults"));
  EXPECT_EQ(hal::get_led_brightness(), 70);
}

TEST_F(SettingsTest, ThresholdsFromAnotherFirmwareAreIgnored) {
  SMC::Settings::load();
  changeEverything();
  ASSERT_TRUE(SMC::Settings::saveNow());
  std::memcpy(record() + 8, "P01", 4);  // Saved by another firmware
  fixChecksum();

  backToDefaults();
  const uint8_t default_threshold = SMC::Fan::getThreshold(0);
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged("fan thresholds left at the defaults (saved for another firmware)"));
  EXPECT_EQ(SMC::Fan::getThreshold(0), default_threshold);
  EXPECT_EQ(hal::get_led_brightness(), 40);  // The rest still loads
}

TEST_F(SettingsTest, InvalidWifiInRecordKeepsDefault) {
  SMC::Settings::load();
  changeEverything();
  ASSERT_TRUE(SMC::Settings::saveNow());
  const size_t ssid_offset = 14 + SMC::Fan::max_thresholds;
  record()[ssid_offset] = 0x01;  // Not printable
  fixChecksum();

  backToDefaults();
  SMC::Settings::load();
  EXPECT_STREQ(SMC::Settings::wifiSsid(), SMC_WIFI_SSID);
  EXPECT_STREQ(SMC::Settings::wifiPassword(), "correct horse");
}

TEST_F(SettingsTest, RequestedSaveIsDoneByUpdate) {
  SMC::Settings::load();
  hal::set_led_brightness(55);
  SMC::Settings::update();
  EXPECT_EQ(fake::flash_writes, 0);  // Only saved on request
  SMC::Settings::requestSave();
  SMC::Settings::update();
  EXPECT_EQ(fake::flash_writes, 1);
  SMC::Settings::update();
  EXPECT_EQ(fake::flash_writes, 1);
}

TEST_F(SettingsTest, WifiValuesAreChecked) {
  EXPECT_FALSE(SMC::Settings::setWifiSsid(""));
  EXPECT_FALSE(SMC::Settings::setWifiSsid(std::string(SMC::Settings::max_ssid_length + 1, 'a').c_str()));
  EXPECT_TRUE(SMC::Settings::setWifiSsid(std::string(SMC::Settings::max_ssid_length, 'a').c_str()));
  EXPECT_FALSE(SMC::Settings::setWifiSsid("tab\there"));
  EXPECT_FALSE(SMC::Settings::setWifiPassword("short"));  // WPA2 needs 8
  EXPECT_TRUE(SMC::Settings::setWifiPassword(""));        // Made from the board ID
  EXPECT_FALSE(SMC::Settings::setWifiPassword(std::string(SMC::Settings::max_password_length + 1, 'a').c_str()));
}

}  // namespace
