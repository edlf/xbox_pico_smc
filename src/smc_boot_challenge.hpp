#ifndef SMC_BOOT_CHALLENGE_HPP
#define SMC_BOOT_CHALLENGE_HPP

#include <cstdint>

namespace SMC {
namespace BootChallenge {

enum class boot_challenge_state {
  initial,
  wait_for_ram_test_result,
  ram_test_ok,
  ram_test_failed,
  challenge_failed_or_no_ram_test_result,
  challenge_wait_for_reply,
  boot_failed,
  lock_up,
  retry_boot,
  check_response,
  reboot,
  rtc_dump_start  // DXF only
};

typedef struct {
  // Challenge seed, read by the host via SMBus 0x1C-0x1F
  uint8_t input_byte0;
  uint8_t input_byte1;
  uint8_t input_byte2;
  uint8_t input_byte3;
  // Expected challenge response, computed from the seed
  uint8_t expected0;
  uint8_t expected1;
  // Response written by the host via SMBus 0x20/0x21
  uint8_t response0;
  uint8_t response1;
} challenge_struct;

// Boot flags
struct flags_struct {
  bool ram_test_submitted;  // SMBus 0x12 written
  bool ram_test_passed;
  bool frag_power_off;               // FRAG: the power button turns off right away
  bool frag_eject;                   // FRAG: the eject button opens the tray directly
  bool challenge_response_received;  // SMBus 0x21 written
};
extern flags_struct flags;

// Enforce the boot challenge (saved setting, SMC_BOOT_CHALLENGE by default):
// a failed RAM test or challenge resets the Xbox. Off, the results are only
// logged. Set only while the Xbox is off.
bool enforced();
void setEnforced(bool on);

void init();
// The last RAM test failed (blinking red LEDs, enforced or not). Cleared by a
// passed RAM test and at power off.
bool ramTestFailed();
// Banks flagged by the failed RAM test (bits 0-3 of SMBus 0x12, assumed one
// per bank), 0 when it passed or no bank was flagged
uint8_t failedRamBanks();

// The CPU answered this boot's challenge correctly. Cleared by a reset.
bool challengePassed();
void update();
void compute();
void seedStandby();
void seedPowerUp();
void printState();

challenge_struct& getChallengeStructRef();

}  // namespace BootChallenge
}  // namespace SMC

#endif  // SMC_BOOT_CHALLENGE_HPP
