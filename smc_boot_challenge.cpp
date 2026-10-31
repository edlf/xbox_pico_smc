#include "smc_boot_challenge.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "pico/stdlib.h"

#include <vector>
#include <string>
#include <cstdio>

namespace SMC {
namespace BootChallenge {

const std::vector<std::string> state_names {
  "Initial",
  "Wait for ram test result",
  "Ram test ok",
  "Ram test failed",
  "Failed or no ram test result",
  "Wait for reply",
  "Boot failed",
  "Lock up",
  "Retry boot",
  "Check response",
  "Challenge passed",
  "Reboot"
};

// Error codes readable via SMBus 0x0F
constexpr uint8_t ERROR_RAM_TEST_FAILED = 0x04;
constexpr uint8_t ERROR_CHALLENGE_FAILED = 0x01;

challenge_struct challenge;
boot_challenge_state state;
uint8_t boot_response_timeout;
uint8_t failure_count;     // Failed boots since the last power cycle
uint8_t failure_countdown; // Power cycles left before locking up (FRAG)
uint8_t rtc_dump_time;

void init() {
  // Same as the PIC's RAM_init (0xB58)
  challenge = {0};
  state = boot_challenge_state::initial;
  boot_response_timeout = 0;
  failure_count = 0;
  failure_countdown = 3;
  rtc_dump_time = 0;
}

// The PIC uses its free running timers as entropy: TMR0 runs at the
// instruction clock, TMR1L at 1/8 of it
static uint8_t fastTimerByte() {
  return (uint8_t)time_us_32();
}

static uint8_t slowTimerByte() {
  return (uint8_t)(time_us_32() >> 3);
}

// Called on every standby loop iteration (0x23B)
void seedStandby() {
  challenge.input_byte0++;
  challenge.input_byte1 = fastTimerByte();
  if (challenge.input_byte1 == 0) {
    challenge.input_byte1 = 1;
  }
}

// Called when the Xbox has powered up (0x250), and to reseed after a reset
void seedPowerUp() {
  challenge.input_byte2 = slowTimerByte() + challenge.input_byte0;
  challenge.input_byte3 = sensors.CPU_temperature ^ challenge.input_byte1;
}

void compute() {
  // Same as the PIC (0xBF6)
  challenge.expected0 = 0x33;
  challenge.expected1 = 0xED;

  for (uint8_t i = 0; i < 4; i++) {
    challenge.expected0 = ((uint8_t)(challenge.input_byte0 << 2) ^ (uint8_t)(challenge.input_byte1 + 0x39) ^
                           (uint8_t)(challenge.input_byte2 >> 2) ^ (uint8_t)(challenge.input_byte3 + 0x63) ^
                           challenge.expected1) +
                          challenge.expected0;
    challenge.expected1 = ((uint8_t)(challenge.input_byte0 + 0x0B) ^ (uint8_t)(challenge.input_byte1 >> 2) ^
                           (uint8_t)(challenge.input_byte2 + 0x1B) ^ challenge.expected0) +
                          challenge.expected1;
  }
}

static std::string hex(uint8_t value) {
  char text[5];
  snprintf(text, sizeof(text), "0x%02X", value);
  return text;
}

static std::string ramTestResult() {
  return "result " + hex(ram_test_response0) + " type " + hex(ram_test_response1);
}

static void clearRamTestResult() {
  flags.bitfield_DATA_71 &= ~0x10;
  flags.bitfield_DATA_71 &= ~0x20;
}

// While running: restart on a reset request, or once a pending reset
// (70.0) is no longer held back by overheating or a shutdown
static void waitForReset() {
  if (flags.bitfield_DATA_73 & 0x01) {
    clearRamTestResult();
    state = boot_challenge_state::initial;
  } else if ((flags.bitfield_DATA_70 & 0x01) && !checkStatusBit(overheated) &&
             !checkStatusBit(prepare_for_shutdown)) {
    clearRamTestResult();
    state = boot_challenge_state::initial;
  }
}

static void lockUp() {
  flags.bitfield_DATA_70 |= 0x01;
  flags.bitfield_DATA_70 &= ~0x02;
  state = boot_challenge_state::lock_up;
}

// Power cycles the Xbox while dumping the RTC, as the PIC does after repeated
// boot failures (0x987). Blocks the main loop like the original.
static bool powerCycleWithRtcDump() {
  auto tick = [] {
    hal::timer1_wait();
    hal::petWatchdog();
  };

  setPowerOkIrq(false);
  hal::turn_off_psu();
  tick();

  uint8_t tries = 50;
  do {
    tick();
    if (!hal::get_power_ok()) {
      break;
    }
  } while (--tries != 0);
  if (tries == 0) {
    return false;
  }

  hal::rtc_dump_on();
  for (uint8_t i = 0; i < 100; i++) {
    tick();
  }
  hal::rtc_dump_off();
  tick();

  hal::turn_on_psu();
  tries = 50;
  while (!hal::get_power_ok()) {
    tick();
    if (--tries == 0) {
      return false;
    }
  }

  setPowerOkIrq(true);
  return true;
}

// Without SMC_BOOT_CHALLENGE the state machine still runs to log the results,
// but a failure never resets the Xbox or locks it up: it carries on with the
// challenge, then waits in challenge_passed for the next reset
void update() {
  switch (state) {
  case boot_challenge_state::initial:
    boot_response_timeout = 12;
    compute();
    state = boot_challenge_state::wait_for_ram_test_result;
    break;

  case boot_challenge_state::wait_for_ram_test_result:
    if (flags.bitfield_DATA_71 & 0x10) {
      /* RAM test results submitted, 71.5 = passed */
      if (flags.bitfield_DATA_71 & 0x20) {
        state = boot_challenge_state::ram_test_ok;
      } else {
        state = boot_challenge_state::ram_test_failed;
      }
    } else if (--boot_response_timeout == 0) {
      debug::print_warn("BOOT: No RAM test result");
#if SMC_BOOT_CHALLENGE
      state = boot_challenge_state::challenge_failed_or_no_ram_test_result;
#else
      state = boot_challenge_state::challenge_wait_for_reply;
#endif
    }
    break;

  case boot_challenge_state::ram_test_ok:
    debug::print_message("BOOT: RAM test passed, " + ramTestResult());
    state = boot_challenge_state::challenge_wait_for_reply;
    break;

  case boot_challenge_state::ram_test_failed:
    debug::print_warn("BOOT: RAM test failed, " + ramTestResult());
#if SMC_BOOT_CHALLENGE
    Led::setRedPhases(0x0F);
    Led::setGreenPhases(0x05);
    bios_response_byte0 = ERROR_RAM_TEST_FAILED;
    state = boot_challenge_state::boot_failed;
#else
    state = boot_challenge_state::challenge_wait_for_reply;
#endif
    break;

  case boot_challenge_state::challenge_failed_or_no_ram_test_result:
    Led::setRedPhases(0x0A);
    Led::setGreenPhases(0x05);
    bios_response_byte0 = ERROR_CHALLENGE_FAILED;
    state = boot_challenge_state::boot_failed;
    break;

  case boot_challenge_state::challenge_wait_for_reply:
    if (!(flags.bitfield_DATA_73 & 0x01) && !(flags.bitfield_DATA_70 & 0x01)) {
      boot_response_timeout = 2;
      state = boot_challenge_state::check_response;
    } else {
      waitForReset();
    }
    break;

  case boot_challenge_state::boot_failed:
    failure_count++;
    if (failure_count <= 3) {
      state = boot_challenge_state::retry_boot;
    } else if (--failure_countdown != 0) {
      failure_count = 0;
      rtc_dump_time = 4;
      flags.bitfield_DATA_70 |= 0x01;
      flags.bitfield_DATA_70 &= ~0x02;
      state = boot_challenge_state::reboot;
    } else {
      lockUp();
    }
    break;

  case boot_challenge_state::lock_up:
    /* FRAG: manual LEDs (error code), stays here until power off */
    flags.bitfield_DATA_70 |= 0x04;
    flags.bitfield_DATA_72 |= 0x20;
    flags.bitfield_DATA_73 |= 0x04;
    break;

  case boot_challenge_state::retry_boot:
    /* Reset the Xbox and try again */
    flags.bitfield_DATA_70 |= 0x01;
    flags.bitfield_DATA_70 |= 0x02;
    flags.bitfield_DATA_70 &= ~0x04;
    clearRamTestResult();
    ram_test_response0 = 0;
    ram_test_response1 = 0;
    state = boot_challenge_state::initial;
    break;

  case boot_challenge_state::check_response:
    if (flags.bitfield_DATA_73 & 0x10) {
      flags.bitfield_DATA_73 &= ~0x10;
      if (challenge.response0 == challenge.expected0 && challenge.response1 == challenge.expected1) {
        debug::print_message("BOOT: Challenge passed");
        state = boot_challenge_state::challenge_passed;
        break;
      }
      debug::print_warn("BOOT: Challenge failed, expected " + hex(challenge.expected0) + " " +
                        hex(challenge.expected1) + " got " + hex(challenge.response0) + " " +
                        hex(challenge.response1));
    } else if (--boot_response_timeout != 0) {
      break;
    } else {
      debug::print_warn("BOOT: No challenge answer");
    }

#if SMC_BOOT_CHALLENGE
    /* Wrong answer or timeout: go straight to a power cycle */
    failure_count = 4;
    state = boot_challenge_state::challenge_failed_or_no_ram_test_result;
#else
    state = boot_challenge_state::challenge_passed;
#endif
    break;

  case boot_challenge_state::challenge_passed:
    if (flags.bitfield_DATA_73 & 0x01) {
      /* Reset requested: new challenge for the next boot */
      clearRamTestResult();
      challenge.input_byte1 = fastTimerByte();
      seedPowerUp();
      state = boot_challenge_state::initial;
    } else {
      waitForReset();
    }
    break;

  case boot_challenge_state::reboot:
    if (--rtc_dump_time != 0) {
      return;
    }
    if (powerCycleWithRtcDump()) {
      state = boot_challenge_state::retry_boot;
    } else {
      lockUp();
    }
    break;

  default:
    state = boot_challenge_state::initial;
    break;
  }
}

challenge_struct& getChallengeStructRef() {
  return challenge;
}

void printState() {
    const size_t bc_state = static_cast<size_t>(state);
    std::string msg = "Boot challenge state [" + std::to_string(bc_state) + "]";

    if (bc_state <= state_names.size()) {
        msg += " " + state_names[bc_state];
    }

    debug::print_message(msg);
}

} // namespace BootChallenge
} // namespace SMC
