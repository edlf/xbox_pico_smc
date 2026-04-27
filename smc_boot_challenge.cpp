#include "smc.hpp"

namespace SMC {

uint8_t failure_count;
uint8_t rtc_time;

void boot_challenge_compute() {
  // Done, untested
  challenge.response0 = 0x33;
  challenge.response1 = 0xed;

  uint8_t challenge_loop_count = 4;
  do {
    challenge.response0 =
        (challenge.input_byte0 << 2 ^ challenge.input_byte1 + 0x39 ^ challenge.input_byte2 >> 2 ^ challenge.input_byte3 + 99U ^ challenge.response1) +
        challenge.response0;
    challenge.status_byte0 = challenge.input_byte0 + 0xbU;
    challenge.status_byte1 = challenge.input_byte1 >> 2;
    challenge.status_byte2 = challenge.input_byte2 + 0x1b;
    challenge.status_byte3 = 0;
    challenge.status_byte4 = challenge.response1;
    challenge.response1 =
        (challenge.input_byte0 + 0xbU ^ challenge.input_byte1 >> 2 ^ challenge.input_byte2 + 0x1b ^ challenge.response0) + challenge.response1;
    challenge_loop_count = challenge_loop_count + 0xff;
  } while (challenge_loop_count != 0);
}

void update_boot_challenge() {
  switch (state.boot_challenge) {
  case boot_challenge_state::initial:
    timers.boot_response_timeout = 12;
    boot_challenge_compute();
    state.boot_challenge = boot_challenge_state::wait_for_ram_test_result;
    break;

  case boot_challenge_state::wait_for_ram_test_result:
    /* Wait for RAM test result submission */
    if (flags.bitfield_DATA_71 & 0x10) {
      /* RAM test results submitted */
      if (flags.bitfield_DATA_71 & 0x20) {
        /* RAM test passed */
        state.boot_challenge = boot_challenge_state::ram_test_ok;
      } else {
        /* RAM test failed */
        state.boot_challenge = boot_challenge_state::ram_test_failed;
      }
    } else if (--timers.boot_response_timeout == 0) {
      state.boot_challenge = boot_challenge_state::challenge_failed_or_no_ram_test_result;
    }
    break;

  case boot_challenge_state::ram_test_ok:
    state.boot_challenge = boot_challenge_state::challenge_wait_for_reply;
    break;

  case boot_challenge_state::ram_test_failed:
    config.LED_red_manual_cycles = 15;
    config.LED_green_manual_cycles = 5;
    flags.bitfield_DATA_70 |= 0x04; // Manual LED control
    state.boot_challenge = boot_challenge_state::boot_failed;
    break;

  case boot_challenge_state::challenge_failed_or_no_ram_test_result:
    config.LED_red_manual_cycles = 10;
    config.LED_green_manual_cycles = 5;
    flags.bitfield_DATA_70 |= 0x04; // Manual LED control
    state.boot_challenge = boot_challenge_state::boot_failed;
    break;

  case boot_challenge_state::challenge_wait_for_reply:
    if (flags.bitfield_DATA_73 & 0x01) {
      /* System reset requested */
      flags.bitfield_DATA_71 &= ~0x10;
      flags.bitfield_DATA_71 &= ~0x20;
      state.boot_challenge = boot_challenge_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* System overheated - stay in this state */
      return;
    } else if ((flags.bitfield_DATA_70 & 0x01) == 0) {
      /* System not overheated */
      timers.boot_response_timeout = 2;
      state.boot_challenge = boot_challenge_state::check_response;
    }
    break;

  case boot_challenge_state::boot_failed:
    failure_count++;

    if (failure_count >= 3) {
      flags.bitfield_DATA_70 |= 0x04; // Manual LED control
      flags.bitfield_DATA_72 |= 0x20; // FRAG flag
      flags.bitfield_DATA_73 |= 0x04; // Locked state
      state.boot_challenge = boot_challenge_state::lock_up;
    } else {
      state.boot_challenge = boot_challenge_state::retry_boot;
    }
    break;

  case boot_challenge_state::lock_up:
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_71 &= ~0x10;
    flags.bitfield_DATA_71 &= ~0x20;
    flags.bitfield_DATA_72 &= ~0x01;
    flags.bitfield_DATA_72 &= ~0x02;
    return;

  case boot_challenge_state::retry_boot:
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    flags.bitfield_DATA_71 &= ~0x10;
    flags.bitfield_DATA_71 &= ~0x20;
    sensors.CPU_temperature = 0;
    sensors.board_temperature = 0;
    state.boot_challenge = boot_challenge_state::initial;
    break;

  case boot_challenge_state::check_response:
    if (flags.bitfield_DATA_73 & 0x10) {
      /* Response received */
      flags.bitfield_DATA_73 &= ~0x10;

      /* Verify both response bytes match expected values */
      if (challenge.response0 == challenge.expected0 && challenge.response1 == challenge.expected1) {
        state.boot_challenge = boot_challenge_state::challenge_passed;
      } else {
        state.boot_challenge = boot_challenge_state::challenge_failed_or_no_ram_test_result;
      }
    } else if (--timers.boot_response_timeout == 0) {
      state.boot_challenge = boot_challenge_state::challenge_failed_or_no_ram_test_result;
    }
    break;

  case boot_challenge_state::challenge_passed:
    if (flags.bitfield_DATA_73 & 0x01) {
      // System reset requested
      flags.bitfield_DATA_71 &= ~0x10;
      flags.bitfield_DATA_71 &= ~0x20;

      /* TODO: Update challenge bytes for next boot */
      state.boot_challenge = boot_challenge_state::initial;
    } else if (flags.bitfield_DATA_70 & 0x01) {
      /* System overheated - stay in this state */
      return;
    }
    break;

  case boot_challenge_state::reboot:
    if (--failure_count != 0) {
      state.boot_challenge = boot_challenge_state::retry_boot;
    } else {
      rtc_time = 4;
      flags.bitfield_DATA_70 |= 0x01;
      flags.bitfield_DATA_70 &= ~0x02;
      state.boot_challenge = boot_challenge_state::reboot;
    }
    break;

  default:
    break;
  }
}

} // namespace SMC
