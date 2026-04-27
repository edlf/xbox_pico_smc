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
  uint8_t input_byte0; /* Input: Challenge seed uint8_t 0 */
  uint8_t input_byte1; /* Input: Challenge seed uint8_t 1 */
  uint8_t input_byte2; /* Input: Challenge seed uint8_t 2 */
  uint8_t input_byte3; /* Input: Challenge seed uint8_t 3 */
  uint8_t expected0;   /* Output: Expected challenge response uint8_t 0 */
  uint8_t expected1;   /* Output: Expected challenge response uint8_t 1 */
  uint8_t response0;   /* Input: Received challenge response uint8_t 0 */
  uint8_t response1;   /* Input: Received challenge response uint8_t 1 */
  uint8_t status_byte0;
  uint8_t status_byte1;
  uint8_t status_byte2;
  uint8_t status_byte3;
  uint8_t status_byte4;
} challenge_struct;

void init();
void resetState();
void update();
void compute();
challenge_struct& getChallengeStructRef();
void printState();

} // namespace BootChallenge
} // namespace SMC

#endif // __SMC_BOOT_CHALLENGE__
