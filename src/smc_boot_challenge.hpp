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
  rtc_dump_start // DXF only
};

typedef struct {
  uint8_t input_byte0; /* Challenge seed, read by the host via SMBus 0x1C-0x1F */
  uint8_t input_byte1;
  uint8_t input_byte2;
  uint8_t input_byte3;
  uint8_t expected0;   /* Expected challenge response, computed from the seed */
  uint8_t expected1;
  uint8_t response0;   /* Response written by the host via SMBus 0x20/0x21 */
  uint8_t response1;
} challenge_struct;

// Boot flags, from the PIC's RAM (address.bit)
struct flags_struct {
  bool ram_test_submitted;          // 71.4 SMBus 0x12 written
  bool ram_test_passed;             // 71.5
  bool frag_power_off;              // 72.5 FRAG: the power button turns off right away
  bool frag_eject;                  // 73.2 FRAG: the eject button opens the tray directly
  bool challenge_response_received; // 73.4 SMBus 0x21 written
};
extern flags_struct flags;

void init();
// The last RAM test failed (blinking red LEDs, enforced or not). Cleared by a
// passed RAM test and at power off.
bool ramTestFailed();
// The CPU answered this boot's challenge correctly. Cleared by a reset.
bool challengePassed();
void update();
void compute();
void seedStandby();
void seedPowerUp();
challenge_struct& getChallengeStructRef();
void printState();

} // namespace BootChallenge
} // namespace SMC

#endif // SMC_BOOT_CHALLENGE_HPP
