#ifndef __SMC_BOOT_CHALLENGE__
#define __SMC_BOOT_CHALLENGE__

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
  challenge_passed,
  reboot
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

void init();
void update();
void compute();
void seedStandby();
void seedPowerUp();
challenge_struct& getChallengeStructRef();
void printState();

} // namespace BootChallenge
} // namespace SMC

#endif // __SMC_BOOT_CHALLENGE__
