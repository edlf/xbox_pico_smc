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
// The BIOS's RAM test (SMBus 0x13 type, sent first, and 0x12 result), one bit
// per RAM chip: 0-3 the four every board has, 4-7 the second four of a 128 MB
// board. Type: the chip is a 16 MB (4Mx32) part. Result: the chip failed the
// pattern check, or doesn't answer at all (so 4-7 are set on a 64 MB board).
struct RamTest {
  uint8_t  fitted;      // The chips the board has: 0x0F, or 0xFF on a 128 MB board
  uint8_t  failed;      // Fitted chips that failed or don't answer
  uint8_t  wrong_size;  // Fitted chips that answer but read as 8 MB next to 16 MB ones: an address line (R11)
  uint16_t megabytes;   // What the BIOS sets up: 128 with eight 16 MB chips, 64 otherwise
  uint16_t board_mb;    // What the board has when it's all working: 128 or 64
  uint8_t  chips_16mb;  // Working chips: 16 MB ones
  uint8_t  chips_8mb;   // and 8 MB ones (wrong size included: 8 MB is what answers)
  uint16_t found_mb;    // Their total
};
RamTest decodeRamTest(uint8_t result, uint8_t type);

// The chips at fault in the failed RAM test (failed or wrong size, as
// decodeRamTest()), 0 when it passed or none was flagged
uint8_t failedRamChips();

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
