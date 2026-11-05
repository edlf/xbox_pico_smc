#ifndef SIMULATOR_HPP
#define SIMULATOR_HPP

// The SMC firmware's real main_loop() running on the PC, on the fake HAL, with
// simple models of what's around it: the PSU, the DVD drive, and the BIOS and
// OS talking to it over SMBus. Time is simulated: run() moves it on, ticks
// take no real time.
//
// One Simulator at a time (the fakes are global). It starts as a Pico that has
// just booted in an Xbox that's off, with an HDTV AV pack and a disc in the drive.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace sim {

// The BIOS and the OS (kernel and dashboard), from what real logs show
struct OsOptions {
  bool     answers = true;               // false: the OS hangs, SMIs go unanswered
  uint32_t answer_delay_ms = 20;         // From the SMI pulse to reading 0x11
  bool     write_19_after_eject = true;  // The kernel writes 0x19 = 0x01 after an eject request
  bool     ram_test_passes = true;       // false: the BIOS reports a failed RAM test (banks 0)
  enum class PowerAnswer { power_off, busy, none } power_answer = PowerAnswer::power_off;
};

struct DriveOptions {
  bool     present = true;  // false: the pins read 0x70, as with no drive
  bool     disc = true;
  uint32_t move_ms = 1200;    // Tray opening or closing
  uint32_t detect_ms = 1000;  // Closed to disc found (0x60) or not (0x40)
  uint32_t unload_ms = 300;   // A spinning disc stops before the tray opens (0x20)
};

class Simulator {
 public:
  Simulator();
  ~Simulator();
  Simulator(const Simulator&) = delete;
  Simulator& operator=(const Simulator&) = delete;

  // Lets the firmware run for this much simulated time
  void run(uint32_t ms);

  // Front panel: pressed for 100 ms, then released
  void pressPower();
  void pressEject();
  void pressBootsel();  // The Pico's BOOTSEL button
  void holdPower(uint32_t ms);

  void pushTray();  // Push an open tray closed by hand
  void setDisc(bool disc);
  void setAvPins(uint8_t pins);  // av_pack encoding, 0x0E: no cable
  void psuDropout(uint32_t ms);  // POWER_OK drops for a while with the PSU on

  // The Xbox's SMBus master, as the OS would: the write is applied by the
  // firmware's main loop, the read is answered at once
  void    smbusWrite(uint8_t command, uint8_t data);
  uint8_t smbusRead(uint8_t command);

  // Change any time between run() calls
  OsOptions    os;
  DriveOptions drive;

  // What happened
  bool    xboxOn() const;     // PSU on
  bool    osRunning() const;  // Booted past the BIOS
  uint8_t trayPins() const;
  int     powerOns() const {
    return power_ons;
  }
  int resets() const {
    return boots - power_ons;
  }  // Warm resets
  int watchdogReboots() const {
    return watchdog_reboots;
  }
  double      seconds() const;
  std::string status() const;

  static const char* trayName(uint8_t pins);

 private:
  struct Stop {};

  void        firmwareThread();
  static void tickHook();
  void        tick();
  void        psuModel();
  void        driveModel();
  void        osModel();
  void        answerSmi();
  void        bootSequence();
  void        setPowerOk(bool ok);

  // Thread handoff: only one side runs at a time
  std::mutex              mutex;
  std::condition_variable cv;
  std::thread             thread;
  uint64_t                run_until_us = 0;
  uint64_t                pauses = 0;  // Times the firmware stopped at run_until_us
  bool                    stopping = false;
  bool                    finished = false;

  uint64_t next_tick_us = 0;

  // PSU
  bool psu_was_on = false;
  int  psu_ticks = 0;
  int  dropout_ticks = 0;
  int  power_ons = 0;

  // Drive
  enum class Mech { closed, unloading, opening, open, closing };
  Mech mech = Mech::closed;
  int  mech_ticks = 0;    // Left in the current movement
  int  detect_ticks = 0;  // Left until the disc is detected, after closing or power on
  bool eject_was_low = true;
  bool drive_powered = false;

  // OS
  enum class OsState { off, bios, kernel, running };
  OsState os_state = OsState::off;
  int     os_ticks = 0;  // Until the next boot step
  int     smi_seen = 0;
  int     smi_ticks = -1;  // Until the OS answers the pending SMI, -1: none
  int     boots = 0;

  int watchdog_reboots = 0;
};

}  // namespace sim

#endif  // SIMULATOR_HPP
