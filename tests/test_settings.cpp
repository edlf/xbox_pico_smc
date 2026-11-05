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
    SMC::Settings::setReportedVersion("auto");
    SMC::Led::setFade(SMC_LED_FADE);
    resetSystemSettings();
  }

  // The settings added in layout version 4, back to the build's defaults
  static void resetSystemSettings() {
    SMC::BootChallenge::setEnforced(SMC_BOOT_CHALLENGE);
    SMC::Video::setAvPackOverride(SMC_AV_PACK_OVERRIDE ? SMC_AV_PACK_MODE : -1);
    debug::setLevel(static_cast<debug::Level>(SMC_DEBUG_LEVEL));
    SMC::Dvd::setSupported(SMC_DVD_SUPPORT);
    SMC::I2CSniffer::setEnabled(SMC_I2C_SNIFFER);
    SMC::ExtraTemp::setNtcConfig({SMC_NTC_SENSOR != 0, SMC_NTC_BETA, SMC_NTC_R25, SMC_NTC_SERIES_R});
    SMC::Fan::setRamp(SMC::Fan::default_ramp);
    SMC::Fan::setTemperatureBypass(false);
  }

  // And all different from them
  static void changeSystemSettings() {
    SMC::BootChallenge::setEnforced(!SMC_BOOT_CHALLENGE);
    ASSERT_TRUE(SMC::Video::setAvPackOverride(SMC_AV_PACK_OVERRIDE ? -1 : 6));
    debug::setLevel(SMC_DEBUG_LEVEL == 4 ? debug::Level::Warnings : debug::Level::Debug);
    SMC::Dvd::setSupported(!SMC_DVD_SUPPORT);
    SMC::I2CSniffer::setEnabled(!SMC_I2C_SNIFFER);
    ASSERT_TRUE(SMC::ExtraTemp::setNtcConfig({!SMC_NTC_SENSOR, 3435, 4700, 22000}));
    ASSERT_TRUE(SMC::Fan::setRamp({2, 3, 200, 500}));
    SMC::Fan::setTemperatureBypass(true);
  }

  // Where the fields of the older layouts end: version 1 after the RGB mode,
  // version 2 after the reported version (4 characters)
  static constexpr size_t v1_fields_end =
      14 + SMC::Fan::max_thresholds + (SMC::Settings::max_ssid_length + 1) + (SMC::Settings::max_password_length + 1) + 1 + 2;
  static constexpr size_t v2_fields_end = v1_fields_end + 4;
  static constexpr size_t v3_fields_end = v2_fields_end + 1;  // The LED fade
  // Six bytes (boot challenge to NTC on), padding, the NTC beta, padding, R25
  // and the series resistor
  static constexpr size_t v4_fields_end = v3_fields_end + 6 + 1 + 2 + 2 + 4 + 4;
  static constexpr size_t v5_fields_end = v4_fields_end + 1 + 1 + 2 + 2;  // The fan ramp

  // Rewrites the saved record as an older layout: the same fields up to its
  // end, then the checksum (4-byte aligned)
  static void makeOlderRecord(const uint8_t layout_version, const size_t fields_end) {
    const uint16_t size = static_cast<uint16_t>(((fields_end + 3) & ~size_t{3}) + 4);
    record()[4] = layout_version;
    record()[5] = 0;
    record()[6] = static_cast<uint8_t>(size);
    record()[7] = static_cast<uint8_t>(size >> 8);
    fixChecksum();
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
    SMC::Led::setFade(!SMC_LED_FADE);
  }

  static void backToDefaults() {
    SMC::RgbLed::setMode(static_cast<SMC::RgbLed::Mode>(SMC_RGB_MODE));
    SMC::RgbLed::setBrightness(SMC_RGB_BRIGHTNESS);
    hal::set_led_brightness(100);
    SMC::Fan::resetThresholds();
    SMC::Settings::resetWifi();
    SMC::Settings::setWebEnabled(true);
    SMC::Led::setFade(SMC_LED_FADE);
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
  EXPECT_EQ(SMC::Led::fadeEnabled(), !SMC_LED_FADE);
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

TEST_F(SettingsTest, ReportedVersionIsCheckedAndSaved) {
  EXPECT_STREQ(SMC::Settings::reportedVersion(), SMC_VERSION_STRING);
  EXPECT_TRUE(SMC::Settings::reportedVersionIsDefault());
  EXPECT_FALSE(SMC::Settings::setReportedVersion("P12"));
  EXPECT_FALSE(SMC::Settings::setReportedVersion("p2l"));
  EXPECT_TRUE(SMC::Settings::reportedVersionIsDefault());

  SMC::Settings::load();
  ASSERT_TRUE(SMC::Settings::setReportedVersion("D01"));
  ASSERT_TRUE(SMC::Settings::saveNow());
  ASSERT_TRUE(SMC::Settings::setReportedVersion("auto"));
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged(", reported version D01"));
  EXPECT_STREQ(SMC::Settings::reportedVersion(), "D01");
  EXPECT_FALSE(SMC::Settings::reportedVersionIsDefault());
}

// Records saved before the reported version existed (layout version 1) still
// load, with the build's version
TEST_F(SettingsTest, VersionOneRecordStillLoads) {
  SMC::Settings::load();
  changeEverything();
  ASSERT_TRUE(SMC::Settings::setReportedVersion("D01"));
  ASSERT_TRUE(SMC::Settings::saveNow());
  makeOlderRecord(1, v1_fields_end);

  backToDefaults();
  ASSERT_TRUE(SMC::Settings::setReportedVersion("auto"));
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged("SETTINGS: Loaded, LED brightness 40%"));
  EXPECT_EQ(hal::get_led_brightness(), 40);
  EXPECT_STREQ(SMC::Settings::wifiSsid(), "Living room");
  EXPECT_EQ(SMC::RgbLed::mode(), SMC::RgbLed::Mode::mirror);
  EXPECT_TRUE(SMC::Settings::reportedVersionIsDefault());
  EXPECT_EQ(SMC::Led::fadeEnabled(), static_cast<bool>(SMC_LED_FADE));
}

// Version 2 records (before the fade setting) load, the fade stays the build's
TEST_F(SettingsTest, VersionTwoRecordStillLoads) {
  SMC::Settings::load();
  changeEverything();
  ASSERT_TRUE(SMC::Settings::setReportedVersion("DXF"));
  ASSERT_TRUE(SMC::Settings::saveNow());
  makeOlderRecord(2, v2_fields_end);

  backToDefaults();
  ASSERT_TRUE(SMC::Settings::setReportedVersion("auto"));
  SMC::Settings::load();
  EXPECT_EQ(hal::get_led_brightness(), 40);
  EXPECT_STREQ(SMC::Settings::reportedVersion(), "DXF");
  EXPECT_EQ(SMC::Led::fadeEnabled(), static_cast<bool>(SMC_LED_FADE));
}

TEST_F(SettingsTest, SystemSettingsAreSaved) {
  SMC::Settings::load();
  changeSystemSettings();
  ASSERT_TRUE(SMC::Settings::saveNow());
  resetSystemSettings();
  SMC::Settings::load();
  EXPECT_EQ(SMC::BootChallenge::enforced(), !SMC_BOOT_CHALLENGE);
  EXPECT_EQ(SMC::Video::avPackOverride(), SMC_AV_PACK_OVERRIDE ? -1 : 6);
  EXPECT_EQ(debug::level(), SMC_DEBUG_LEVEL == 4 ? debug::Level::Warnings : debug::Level::Debug);
  EXPECT_EQ(SMC::Dvd::supported(), !SMC_DVD_SUPPORT);
  EXPECT_EQ(SMC::I2CSniffer::enabled(), !SMC_I2C_SNIFFER);
  const SMC::ExtraTemp::NtcConfig& ntc = SMC::ExtraTemp::ntcConfig();
  EXPECT_EQ(ntc.enabled, !SMC_NTC_SENSOR);
  EXPECT_EQ(ntc.beta, 3435);
  EXPECT_EQ(ntc.r25, 4700u);
  EXPECT_EQ(ntc.series_r, 22000u);
  EXPECT_TRUE(fake::logged("SETTINGS: Boot challenge "));
  EXPECT_EQ(SMC::Fan::ramp().up, 2);
  EXPECT_EQ(SMC::Fan::ramp().down, 3);
  EXPECT_EQ(SMC::Fan::ramp().glide_up_ms, 200);
  EXPECT_EQ(SMC::Fan::ramp().glide_down_ms, 500);
  EXPECT_TRUE(fake::logged("SETTINGS: Fan ramp up x2 down x3, glide up 200 ms down 500 ms per step"));
  EXPECT_TRUE(SMC::Fan::temperatureBypass());
  EXPECT_TRUE(fake::logged("SETTINGS: Sensor bypass on"));
}

// Version 5 records (before the sensor bypass) load, the bypass stays off
TEST_F(SettingsTest, VersionFiveRecordStillLoads) {
  SMC::Settings::load();
  changeSystemSettings();
  ASSERT_TRUE(SMC::Settings::saveNow());
  makeOlderRecord(5, v5_fields_end);

  resetSystemSettings();
  SMC::Settings::load();
  EXPECT_EQ(SMC::Fan::ramp().up, 2);  // Version 5 has it
  EXPECT_FALSE(SMC::Fan::temperatureBypass());
}

// Version 4 records (before the fan ramp) load, the ramp stays the default
TEST_F(SettingsTest, VersionFourRecordStillLoads) {
  SMC::Settings::load();
  changeSystemSettings();
  ASSERT_TRUE(SMC::Settings::saveNow());
  makeOlderRecord(4, v4_fields_end);

  resetSystemSettings();
  SMC::Settings::load();
  EXPECT_EQ(SMC::Dvd::supported(), !SMC_DVD_SUPPORT);  // Version 4 has it
  EXPECT_EQ(SMC::ExtraTemp::ntcConfig().series_r, 22000u);
  EXPECT_EQ(SMC::Fan::ramp().up, 1);
  EXPECT_EQ(SMC::Fan::ramp().glide_down_ms, 0);
}

// Version 3 records (before these settings) load, they stay the build's
TEST_F(SettingsTest, VersionThreeRecordStillLoads) {
  SMC::Settings::load();
  changeEverything();
  changeSystemSettings();
  ASSERT_TRUE(SMC::Settings::saveNow());
  makeOlderRecord(3, v3_fields_end);

  backToDefaults();
  resetSystemSettings();
  SMC::Settings::load();
  EXPECT_EQ(hal::get_led_brightness(), 40);
  EXPECT_EQ(SMC::Led::fadeEnabled(), !SMC_LED_FADE);
  EXPECT_EQ(SMC::Dvd::supported(), static_cast<bool>(SMC_DVD_SUPPORT));
  EXPECT_EQ(SMC::ExtraTemp::ntcConfig().beta, SMC_NTC_BETA);
}

TEST_F(SettingsTest, NtcValuesAreChecked) {
  SMC::ExtraTemp::NtcConfig ntc = SMC::ExtraTemp::ntcConfig();
  ntc.beta = 999;
  EXPECT_FALSE(SMC::ExtraTemp::setNtcConfig(ntc));
  ntc.beta = 3950;
  ntc.series_r = 99;
  EXPECT_FALSE(SMC::ExtraTemp::setNtcConfig(ntc));
  ntc.series_r = 1000000;
  EXPECT_TRUE(SMC::ExtraTemp::setNtcConfig(ntc));
}

TEST_F(SettingsTest, FadeIsSaved) {
  SMC::Settings::load();
  SMC::Led::setFade(!SMC_LED_FADE);
  ASSERT_TRUE(SMC::Settings::saveNow());
  SMC::Led::setFade(SMC_LED_FADE);
  SMC::Settings::load();
  EXPECT_TRUE(fake::logged(SMC_LED_FADE ? ", LED fade off" : ", LED fade on"));
  EXPECT_EQ(SMC::Led::fadeEnabled(), !SMC_LED_FADE);
}

// Below min_threshold (40 °C) the Xbox would see an overheat at every start
TEST_F(SettingsTest, ThresholdsBelowTheMinimumAreRefused) {
  EXPECT_FALSE(SMC::Fan::setThreshold(0, SMC::Fan::min_threshold - 1));
  EXPECT_TRUE(SMC::Fan::setThreshold(0, SMC::Fan::min_threshold));
  SMC::Fan::resetThresholds();
}

// A threshold saved by older firmware, which allowed down to 30 °C, is left
// at the default
TEST_F(SettingsTest, SavedThresholdBelowTheMinimumKeepsDefault) {
  SMC::Settings::load();
  changeEverything();
  ASSERT_TRUE(SMC::Settings::saveNow());
  record()[14] = 35;  // Threshold 0
  fixChecksum();

  backToDefaults();
  const uint8_t default_threshold = SMC::Fan::getThreshold(0);
  SMC::Settings::load();
  EXPECT_EQ(SMC::Fan::getThreshold(0), default_threshold);
  EXPECT_EQ(hal::get_led_brightness(), 40);  // The rest still loads
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

// The access point uses the saved password, or while none is saved one made
// from the board ID (also reported over USB config)
TEST_F(SettingsTest, AccessPointPasswordFallsBackToTheBoardId) {
  char password[64];
  SMC::Settings::boardWifiPassword(password, sizeof(password));
  EXPECT_STREQ(password, "smcx-E6614103E7123456");

  ASSERT_TRUE(SMC::Settings::setWifiPassword(""));
  EXPECT_TRUE(SMC::Settings::accessPointPassword(password, sizeof(password)));
  EXPECT_STREQ(password, "smcx-E6614103E7123456");

  ASSERT_TRUE(SMC::Settings::setWifiPassword("correct horse"));
  EXPECT_FALSE(SMC::Settings::accessPointPassword(password, sizeof(password)));
  EXPECT_STREQ(password, "correct horse");
}

}  // namespace
