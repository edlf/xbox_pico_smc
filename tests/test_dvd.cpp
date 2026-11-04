// DVD tray state machines (src/smc_dvd.cpp): the tray state debounce, the eject
// pulse, the open/close decision and the "tray moving" tracking

#include <gtest/gtest.h>

#include <cstdint>

#include "fakes.hpp"
#include "smc.hpp"

namespace {

using SMC::Dvd::drive_tray_state;

class DvdTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fake::reset();
    SMC::globals_init();
    SMC::Dvd::init();
  }

  static void debounceTicks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::Dvd::updateTrayDebounce();
    }
  }

  static void ejectPulseTicks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::Dvd::updateEjectPulse();
    }
  }

  static void movingTicks(int n) {
    for (int i = 0; i < n; i++) {
      SMC::Dvd::updateTrayMoving();
    }
  }

  // The debounce ignores the tray for 39 ticks after it starts (as P2L), then
  // watches it
  static void pastStartupDelay() {
    debounceTicks(40);
  }

  static void setTrayPins(drive_tray_state state) {
    fake::hal.tray_state = static_cast<uint8_t>(state);
  }

  // The tray as the OS opened it: position open, nothing pending
  static void openTray() {
    SMC::Dvd::flags.tray_eject_request = true;
    SMC::Dvd::updateEjectRequests();
    ASSERT_FALSE(SMC::Dvd::isTrayClosing());
    SMC::Dvd::flags.eject_pulse = false;
    SMC::Dvd::flags.eject_button_event = false;
  }
};

// Tray state debounce

TEST_F(DvdTest, TrayIgnoredDuringStartupDelay) {
  setTrayPins(drive_tray_state::opening);
  pastStartupDelay();
  EXPECT_EQ(SMC::Dvd::sensors.tray_status, drive_tray_state::closed);
}

TEST_F(DvdTest, ChangeAppliedAfterTwoMatchingReads) {
  setTrayPins(drive_tray_state::opening);
  pastStartupDelay();
  debounceTicks(2);  // Differs, then the same again
  EXPECT_EQ(SMC::Dvd::sensors.tray_status, drive_tray_state::closed);
  debounceTicks(1);  // Applied
  EXPECT_EQ(SMC::Dvd::sensors.tray_status, drive_tray_state::opening);
  EXPECT_TRUE(SMC::SMI::flags.tray_opening_event);
}

TEST_F(DvdTest, OneReadGlitchIsIgnored) {
  pastStartupDelay();
  setTrayPins(drive_tray_state::opening);
  debounceTicks(1);
  setTrayPins(drive_tray_state::closed);
  debounceTicks(5);
  EXPECT_EQ(SMC::Dvd::sensors.tray_status, drive_tray_state::closed);
  EXPECT_FALSE(SMC::SMI::flags.tray_opening_event);
}

TEST_F(DvdTest, MediaStateRaisesMediaEvent) {
  pastStartupDelay();
  setTrayPins(drive_tray_state::disc_detected);
  debounceTicks(3);
  EXPECT_EQ(SMC::Dvd::sensors.tray_status, drive_tray_state::disc_detected);
  EXPECT_TRUE(SMC::SMI::flags.tray_media_event);
}

TEST_F(DvdTest, NoEventsUntilAnotherStateEnablesThem) {
  SMC::Dvd::flags.tray_events_enabled = false;
  pastStartupDelay();
  setTrayPins(drive_tray_state::opening);
  debounceTicks(3);
  EXPECT_EQ(SMC::Dvd::sensors.tray_status, drive_tray_state::opening);
  EXPECT_FALSE(SMC::SMI::flags.tray_opening_event);

  setTrayPins(drive_tray_state::open);  // Not an event state: enables them
  debounceTicks(3);
  EXPECT_TRUE(SMC::Dvd::flags.tray_events_enabled);
  setTrayPins(drive_tray_state::closing);
  debounceTicks(3);
  setTrayPins(drive_tray_state::closed);
  debounceTicks(3);
  EXPECT_TRUE(SMC::SMI::flags.tray_state_00_event);
}

// Eject pulse

TEST_F(DvdTest, EjectPulseLastsFiveTicks) {
  SMC::Dvd::flags.eject_pulse = true;
  ejectPulseTicks(1);
  EXPECT_FALSE(fake::hal.dvd_eject);
  ejectPulseTicks(1);
  EXPECT_TRUE(fake::hal.dvd_eject);
  ejectPulseTicks(4);
  EXPECT_TRUE(fake::hal.dvd_eject);
  ejectPulseTicks(1);
  EXPECT_FALSE(fake::hal.dvd_eject);
  EXPECT_FALSE(SMC::Dvd::flags.eject_pulse);
}

// Open / close decision

TEST_F(DvdTest, OsEjectRequestOpensClosedTray) {
  SMC::Dvd::flags.tray_eject_request = true;
  SMC::Dvd::updateEjectRequests();
  EXPECT_TRUE(SMC::Dvd::flags.eject_pulse);
  EXPECT_TRUE(SMC::Dvd::flags.eject_button_event);
  EXPECT_FALSE(SMC::Dvd::flags.tray_eject_request);
  EXPECT_FALSE(SMC::Dvd::isTrayClosing());
}

TEST_F(DvdTest, NoEjectWhileDriveIsOff) {
  setTrayPins(drive_tray_state::drive_off);
  SMC::Dvd::flags.tray_eject_request = true;
  SMC::Dvd::updateEjectRequests();
  EXPECT_FALSE(SMC::Dvd::flags.eject_pulse);
  EXPECT_TRUE(SMC::Dvd::flags.tray_eject_request);  // Still pending
  EXPECT_TRUE(SMC::Dvd::isTrayClosing());
}

TEST_F(DvdTest, EjectButtonClosesOpenTray) {
  openTray();
  SMC::Dvd::sensors.tray_status = drive_tray_state::open;
  SMC::Dvd::flags.eject_change_requested = true;
  SMC::Dvd::updateEjectRequests();
  EXPECT_TRUE(SMC::Dvd::flags.eject_pulse);
  EXPECT_FALSE(SMC::Dvd::flags.eject_change_requested);
  EXPECT_TRUE(SMC::Dvd::isTrayClosing());
}

TEST_F(DvdTest, ShutdownClosesOpenTray) {
  openTray();
  SMC::Dvd::sensors.tray_status = drive_tray_state::open;
  SMC::SMI::flags.prepare_for_shutdown = true;
  SMC::Dvd::updateEjectRequests();
  EXPECT_TRUE(SMC::Dvd::flags.eject_pulse);
  EXPECT_TRUE(SMC::Dvd::isTrayClosing());
}

TEST_F(DvdTest, EjectButtonOnClosedTrayOnlyAsksTheOs) {
  SMC::Dvd::flags.eject_change_requested = true;
  SMC::Dvd::updateEjectRequests();
  EXPECT_FALSE(SMC::Dvd::flags.eject_pulse);  // The OS opens it with SMBus 0x0C
  EXPECT_TRUE(SMC::Dvd::flags.tray_open_expected);
  EXPECT_TRUE(SMC::Dvd::isTrayClosing());
}

// P2L: a tray pushed closed by hand (reports closed while not moving) is followed
TEST_F(DvdTest, OpenTrayReportedClosedIsFollowed) {
  openTray();
  SMC::Dvd::sensors.tray_status = drive_tray_state::closed;
  SMC::Dvd::updateEjectRequests();
  EXPECT_TRUE(SMC::Dvd::isTrayClosing());
  EXPECT_FALSE(SMC::Dvd::flags.eject_pulse);
}

TEST_F(DvdTest, ClosedReportIgnoredWhileTrayMoves) {
  openTray();
  SMC::Dvd::flags.tray_moving = true;
  SMC::Dvd::sensors.tray_status = drive_tray_state::closed;
  SMC::Dvd::updateEjectRequests();
  EXPECT_FALSE(SMC::Dvd::isTrayClosing());
}

TEST_F(DvdTest, ClosingStateEndsOpenPhase) {
  openTray();
  SMC::Dvd::flags.tray_moving = true;
  SMC::Dvd::sensors.tray_status = drive_tray_state::closing;
  SMC::Dvd::updateEjectRequests();
  EXPECT_TRUE(SMC::Dvd::isTrayClosing());
}

// Tray moving (SMBus 0x03 bit 0)

TEST_F(DvdTest, EjectPressTracksMoveUntilOpen) {
  SMC::Dvd::flags.eject_button_event = true;
  movingTicks(2);  // initial -> eject, then moving
  EXPECT_TRUE(SMC::Dvd::flags.tray_moving);

  SMC::Dvd::sensors.tray_status = drive_tray_state::opening;
  movingTicks(2);  // eject -> wait, which clears the button event
  EXPECT_TRUE(SMC::Dvd::flags.tray_moving);
  EXPECT_FALSE(SMC::Dvd::flags.eject_button_event);

  SMC::Dvd::sensors.tray_status = drive_tray_state::open;
  movingTicks(2);  // wait -> initial
  EXPECT_FALSE(SMC::Dvd::flags.tray_moving);
}

// P2L: closing (0x50) ends the eject phase even while the tray is expected to open
TEST_F(DvdTest, ClosingEndsEjectPhase) {
  SMC::Dvd::flags.eject_button_event = true;
  SMC::Dvd::flags.tray_open_expected = true;
  movingTicks(2);
  SMC::Dvd::sensors.tray_status = drive_tray_state::closing;
  movingTicks(1);
  EXPECT_FALSE(SMC::Dvd::flags.tray_open_expected);
}

TEST_F(DvdTest, EjectPhaseTimesOutOnceOsIsReady) {
  SMC::Dvd::flags.eject_button_event = true;
  SMC::SMI::flags.os_ready = true;
  movingTicks(1 + 254);  // Into the eject phase, then 254 of its 255 ticks: the tray never moves
  EXPECT_TRUE(SMC::Dvd::flags.eject_button_event);
  movingTicks(2);  // Timed out: wait, which clears the button event
  EXPECT_FALSE(SMC::Dvd::flags.eject_button_event);
  EXPECT_TRUE(SMC::Dvd::flags.tray_moving);
}

TEST_F(DvdTest, DriveMovingOnItsOwnIsTracked) {
  SMC::Dvd::sensors.tray_status = drive_tray_state::closing;
  movingTicks(2);
  EXPECT_TRUE(SMC::Dvd::flags.tray_moving);
  SMC::Dvd::sensors.tray_status = drive_tray_state::no_disc;
  movingTicks(2);
  EXPECT_FALSE(SMC::Dvd::flags.tray_moving);
}

}  // namespace
