// The USB config shell's commands (src/usb_commands.cpp): each setting set,
// refused when out of range, and the replies the tools read

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "config.hpp"
#include "fakes.hpp"
#include "hal.hpp"
#include "smc.hpp"
#include "usb_commands.hpp"

namespace {

std::string reply;  // What the last command printed

class UsbCommandsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    fake::eraseFlash();
    SMC::Fan::init();
    SMC::Settings::resetToDefaults();
    SMC::Settings::load();
  }

  void TearDown() override {
    SMC::Settings::resetToDefaults();
  }

  static UsbConfig::Action run(const char* line) {
    char buffer[96];
    snprintf(buffer, sizeof(buffer), "%s", line);
    reply.clear();
    return UsbConfig::command(buffer, [](const char* text) { reply += text; });
  }

  // The command answered OK
  static bool ok(const char* line) {
    run(line);
    return reply.size() >= 3 && reply.compare(reply.size() - 3, 3, "OK\n") == 0;
  }

  // The command was refused with this reason
  static bool refused(const char* line, const char* reason) {
    run(line);
    return reply == std::string("ERR ") + reason + "\n";
  }
};

TEST_F(UsbCommandsTest, OnOffSettings) {
  ASSERT_TRUE(ok("set fade off"));
  EXPECT_FALSE(SMC::Led::fadeEnabled());
  ASSERT_TRUE(ok("set web on"));
  EXPECT_TRUE(SMC::Settings::webEnabled());
  ASSERT_TRUE(ok("set boot-challenge on"));
  EXPECT_TRUE(SMC::BootChallenge::enforced());
  ASSERT_TRUE(ok("set no-dvd on"));
  EXPECT_FALSE(SMC::Dvd::supported());  // Inverted: on means no drive
  ASSERT_TRUE(ok("set no-dvd off"));
  EXPECT_TRUE(SMC::Dvd::supported());
  ASSERT_TRUE(ok("set sniffer on"));
  EXPECT_TRUE(SMC::I2CSniffer::enabled());
  ASSERT_TRUE(ok("set sensor-bypass on"));
  EXPECT_TRUE(SMC::Fan::temperatureBypass());
  ASSERT_TRUE(ok("set eject-button off"));
  EXPECT_FALSE(SMC::FrontPanelSW::ejectButtonEnabled());
  ASSERT_TRUE(ok("set ntc on"));
  EXPECT_TRUE(SMC::ExtraTemp::ntcConfig().enabled);
  ASSERT_TRUE(ok("set ntc-shutdown on"));
  EXPECT_TRUE(SMC::ExtraTemp::ntcShutdown().enabled);

  EXPECT_TRUE(refused("set fade maybe", "fade must be on or off"));
  EXPECT_TRUE(refused("set no-dvd 1", "no-dvd must be on or off"));
}

// BOOTSEL is the power button while the front panel's is off: it can't be
// turned off then
TEST_F(UsbCommandsTest, BootselStaysOnWithoutThePowerButton) {
  ASSERT_TRUE(ok("set bootsel-power on"));
  ASSERT_TRUE(ok("set power-button off"));
  EXPECT_FALSE(SMC::FrontPanelSW::powerButtonEnabled());
  EXPECT_TRUE(refused("set bootsel-power off", "bootsel-power is needed while the power button is off: turn the power button on first"));
  EXPECT_TRUE(SMC::FrontPanelSW::bootselPowerSetting());
  ASSERT_TRUE(ok("set power-button on"));
  ASSERT_TRUE(ok("set bootsel-power off"));
  EXPECT_FALSE(SMC::FrontPanelSW::bootselPowerSetting());
}

TEST_F(UsbCommandsTest, NumberSettings) {
  ASSERT_TRUE(ok("set brightness 40"));
  EXPECT_EQ(hal::get_led_brightness(), 40);
  ASSERT_TRUE(ok("set rgb-brightness 30"));
  EXPECT_EQ(SMC::RgbLed::brightness(), 30);
  ASSERT_TRUE(ok("set fan-ramp-up 2"));
  ASSERT_TRUE(ok("set fan-ramp-down 3"));
  ASSERT_TRUE(ok("set fan-glide-up 200"));
  ASSERT_TRUE(ok("set fan-glide-down 1000"));
  EXPECT_EQ(SMC::Fan::ramp().up, 2);
  EXPECT_EQ(SMC::Fan::ramp().down, 3);
  EXPECT_EQ(SMC::Fan::ramp().glide_up_ms, 200);
  EXPECT_EQ(SMC::Fan::ramp().glide_down_ms, 1000);
  ASSERT_TRUE(ok("set bypass-cpu-offset -30"));
  ASSERT_TRUE(ok("set bypass-board-offset 30"));
  EXPECT_EQ(SMC::Fan::bypassOffsets().cpu, -30);
  EXPECT_EQ(SMC::Fan::bypassOffsets().board, 30);
  ASSERT_TRUE(ok("set ntc-beta 3435"));
  ASSERT_TRUE(ok("set ntc-r25 4700"));
  ASSERT_TRUE(ok("set ntc-series 1000000"));
  ASSERT_TRUE(ok("set ntc-shutdown-temp 95"));
  EXPECT_EQ(SMC::ExtraTemp::ntcShutdown().limit_c, 95);
  EXPECT_EQ(SMC::ExtraTemp::ntcConfig().beta, 3435);
  EXPECT_EQ(SMC::ExtraTemp::ntcConfig().r25, 4700u);
  EXPECT_EQ(SMC::ExtraTemp::ntcConfig().series_r, 1000000u);
}

TEST_F(UsbCommandsTest, NumbersOutOfRangeAreRefusedAndKept) {
  EXPECT_TRUE(refused("set brightness 0", "brightness must be 1 to 100"));
  EXPECT_TRUE(refused("set brightness 101", "brightness must be 1 to 100"));
  EXPECT_TRUE(refused("set brightness lots", "brightness must be 1 to 100"));
  EXPECT_EQ(hal::get_led_brightness(), SMC_LED_BRIGHTNESS);
  EXPECT_TRUE(refused("set fan-ramp-up 6", "fan-ramp-up must be 1 to 5"));
  EXPECT_TRUE(refused("set fan-glide-down 1001", "fan-glide-down must be 0 to 1000"));
  EXPECT_TRUE(refused("set bypass-cpu-offset 31", "bypass-cpu-offset must be -30 to 30"));
  EXPECT_TRUE(refused("set ntc-beta 999", "ntc-beta must be 1000 to 10000"));
  EXPECT_TRUE(refused("set ntc-series 99", "ntc-series must be 100 to 1000000"));
  EXPECT_TRUE(refused("set ntc-shutdown-temp 39", "ntc-shutdown-temp must be 40 to 120"));
  EXPECT_EQ(SMC::Fan::ramp().up, SMC::Fan::default_ramp.up);
  EXPECT_EQ(SMC::Fan::bypassOffsets().cpu, SMC::Fan::default_bypass_offsets.cpu);
}

TEST_F(UsbCommandsTest, OtherSettings) {
  ASSERT_TRUE(ok("set av-pack 4"));
  EXPECT_EQ(SMC::Video::avPackOverride(), 4);
  ASSERT_TRUE(ok("set av-pack detect"));
  EXPECT_EQ(SMC::Video::avPackOverride(), -1);
  EXPECT_TRUE(refused("set av-pack 7", "av-pack must be detect or 0-6"));
  ASSERT_TRUE(ok("set log-level warnings"));
  EXPECT_EQ(debug::level(), debug::Level::Warnings);
  EXPECT_TRUE(refused("set log-level loud", "log-level must be silent, critical, warnings, info or debug"));
  ASSERT_TRUE(ok("set rgb-mode mirror"));
  EXPECT_EQ(SMC::RgbLed::mode(), SMC::RgbLed::Mode::mirror);
  EXPECT_TRUE(refused("set rgb-mode rainbow", "rgb-mode must be off, boot or mirror"));
  ASSERT_TRUE(ok("set report-version D01"));
  EXPECT_STREQ(SMC::Settings::reportedVersion(), "D01");
  EXPECT_TRUE(refused("set report-version X99", "report-version must be auto, P01, P11, D01, DXF or P2L"));
}

TEST_F(UsbCommandsTest, ThresholdsWifiAndUnknownCommands) {
  const uint8_t threshold = SMC::Fan::getThreshold(0);
  ASSERT_TRUE(ok(("set threshold 0 " + std::to_string(threshold - 1)).c_str()));
  EXPECT_EQ(SMC::Fan::getThreshold(0), threshold - 1);
  EXPECT_TRUE(refused(("set threshold 0 " + std::to_string(threshold + 1)).c_str(), "no such threshold, or out of range (min to default)"));
  EXPECT_TRUE(refused("set threshold 99 50", "no such threshold, or out of range (min to default)"));

  ASSERT_TRUE(ok("set ssid Living room"));  // The rest of the line, spaces too
  EXPECT_STREQ(SMC::Settings::wifiSsid(), "Living room");
  ASSERT_TRUE(ok("set password correct horse"));
  EXPECT_STREQ(SMC::Settings::wifiPassword(), "correct horse");
  ASSERT_TRUE(ok("set password"));  // Empty: the board ID one
  EXPECT_STREQ(SMC::Settings::wifiPassword(), "");
  EXPECT_TRUE(refused("set password short", "Wi-Fi password must be 8-63 printable characters, or empty for the board ID one"));

  EXPECT_TRUE(refused("set warp-drive on", "unknown command, try help"));
  EXPECT_TRUE(refused("launch", "unknown command, try help"));
  EXPECT_EQ(run(""), UsbConfig::Action::none);
  EXPECT_EQ(reply, "");  // An empty line gets no reply
}

TEST_F(UsbCommandsTest, GetHelpAndTheRest) {
  ASSERT_TRUE(ok("get"));
  EXPECT_EQ(reply.rfind("firmware ", 0), 0u);
  EXPECT_NE(reply.find("\ngit host\n"), std::string::npos);
  EXPECT_NE(reply.find("\nbrightness 100\n"), std::string::npos);
  EXPECT_NE(reply.find("\nthreshold 0 "), std::string::npos);
  ASSERT_TRUE(ok("help"));
  EXPECT_NE(reply.find("set no-dvd <on|off>"), std::string::npos);

  ASSERT_TRUE(ok("set brightness 40"));
  ASSERT_TRUE(ok("save"));
  ASSERT_TRUE(ok("defaults"));
  EXPECT_EQ(hal::get_led_brightness(), SMC_LED_BRIGHTNESS);

  EXPECT_EQ(run("reboot"), UsbConfig::Action::reboot);
  EXPECT_EQ(reply, "OK\n");  // Before the restart
  EXPECT_EQ(run("bootloader"), UsbConfig::Action::bootloader);
  EXPECT_EQ(reply, "OK\n");
}

}  // namespace
