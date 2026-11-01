#include "smc_boot_challenge.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "utils.hpp"
#include "pico/stdlib.h"

#include <iterator>
#include <string>

namespace SMC {
namespace BootChallenge {

flags_struct flags;

constexpr const char* state_names[] = {
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
  "Reboot",
  "RTC dump start"
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
// As P2L (bit 26.3): the answer is only checked after a passed RAM test
static bool challenge_check_pending;

void init() {
  // Same as the PIC's RAM_init (0xB58)
  challenge = {0};
  state = boot_challenge_state::initial;
  boot_response_timeout = 0;
  failure_count = 0;
  failure_countdown = 3;
  rtc_dump_time = 0;
  challenge_check_pending = false;
}

// The PIC uses its free running timers as entropy: TMR0 runs at the
// instruction clock, TMR1L at 1/8 of it
static uint8_t fastTimerByte() {
  return (uint8_t)time_us_32();
}

static uint8_t slowTimerByte() {
  return (uint8_t)(time_us_32() >> 3);
}

// Called on every standby loop iteration (0x23B). D01/DXF have no challenge
// and leave the bytes alone, also in seedPowerUp().
void seedStandby() {
#if SMC_FIRMWARE_NO_CHALLENGE
  return;
#endif
  challenge.input_byte0++;
  challenge.input_byte1 = fastTimerByte();
  if (challenge.input_byte1 == 0) {
    challenge.input_byte1 = 1;
  }
}

// Called when the Xbox has powered up (0x250), and to reseed after a reset
void seedPowerUp() {
#if SMC_FIRMWARE_NO_CHALLENGE
  return;
#endif
  challenge.input_byte2 = slowTimerByte() + challenge.input_byte0;
  challenge.input_byte3 = Fan::sensors.CPU_temperature ^ challenge.input_byte1;
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

static std::string ramTestResult() {
  return "result " + utils::toHex(ram_test_response0) + " type " + utils::toHex(ram_test_response1);
}

static void clearRamTestResult() {
  flags.ram_test_submitted = false;
  flags.ram_test_passed = false;
}

// As P2L (0xDA2): new challenge on every reset, and the power cycle count
// starts again. D01/DXF have no challenge and leave the bytes alone.
static void newChallengeAfterReset() {
  clearRamTestResult();
#if !SMC_FIRMWARE_NO_CHALLENGE
  challenge.input_byte1 = fastTimerByte();
#endif
  seedPowerUp();
  failure_countdown = 3;
  state = boot_challenge_state::initial;
}

static void lockUp() {
  PLL_Reset::flags.cold_reset_request = true;
  PLL_Reset::flags.run_after_reset = false;
  state = boot_challenge_state::lock_up;
}

#if !SMC_FIRMWARE_DXF // DXF only pulses RTC dump, without a power cycle
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
#endif

// Follows P2L for every firmware. Without SMC_BOOT_CHALLENGE the state machine
// still runs to log the results, but a failure never resets the Xbox or locks
// it up: it carries on, then waits in challenge_wait_for_reply for the next
// reset
void update() {
  switch (state) {
  case boot_challenge_state::initial:
    // As P2L (0xD5F): 15 ticks for the RAM test (the PIC waits 12), lets the
    // system run and restarts the tray tracking
    boot_response_timeout = 15;
    PLL_Reset::flags.run_after_reset = true;
    Dvd::restartTrayTracking();
#if !SMC_FIRMWARE_NO_CHALLENGE
    compute(); // D01/DXF (0x8DA) skip it
#endif
    state = boot_challenge_state::wait_for_ram_test_result;
    break;

  case boot_challenge_state::wait_for_ram_test_result:
    if (flags.ram_test_submitted) {
      /* RAM test results submitted */
      if (flags.ram_test_passed) {
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
    challenge_check_pending = !SMC_FIRMWARE_NO_CHALLENGE; // D01/DXF only check the RAM test
    state = boot_challenge_state::challenge_wait_for_reply;
    break;

  case boot_challenge_state::ram_test_failed:
    debug::print_warn("BOOT: RAM test failed, " + ramTestResult());
#if SMC_BOOT_CHALLENGE
    Led::setRedPhases(0x0F);
    Led::setGreenPhases(0x05);
    Led::setBootFailurePattern(0x0F, 0x05);
    bios_response_byte0 = ERROR_RAM_TEST_FAILED;
    state = boot_challenge_state::boot_failed;
#else
    state = boot_challenge_state::challenge_wait_for_reply;
#endif
    break;

  case boot_challenge_state::challenge_failed_or_no_ram_test_result:
    Led::setRedPhases(0x0A);
    Led::setGreenPhases(0x05);
    Led::setBootFailurePattern(0x0A, 0x05);
    bios_response_byte0 = ERROR_CHALLENGE_FAILED;
    state = boot_challenge_state::boot_failed;
    break;

  case boot_challenge_state::challenge_wait_for_reply:
    // As P2L (0xD96): check the answer once after a passed RAM test, otherwise
    // wait for a reset and start a new challenge
    if (PLL_Reset::flags.warm_reset_request) {
      newChallengeAfterReset();
    } else if (!PLL_Reset::flags.cold_reset_request) {
      if (challenge_check_pending) {
        boot_response_timeout = 4;
        state = boot_challenge_state::check_response;
      }
    } else if (!Fan::flags.overheated && !SMI::flags.prepare_for_shutdown) {
      newChallengeAfterReset();
    }
    break;

  case boot_challenge_state::boot_failed:
    AudioClamp::flags.audio_clamp_request = true; // Also mutes the audio, as P2L (0xDCA)
    failure_count++;
    if (failure_count <= 3) {
      state = boot_challenge_state::retry_boot;
    } else if (--failure_countdown != 0) {
      failure_count = 0;
      PLL_Reset::flags.cold_reset_request = true;
      PLL_Reset::flags.run_after_reset = false;
#if SMC_FIRMWARE_DXF
      rtc_dump_time = 100; // DXF (0x94D): RTC dump while held in reset
      state = boot_challenge_state::rtc_dump_start;
#else
      rtc_dump_time = 4;
      state = boot_challenge_state::reboot;
#endif
    } else {
      lockUp();
    }
    break;

  case boot_challenge_state::lock_up:
    /* FRAG: error code LEDs (top priority, see Led::setBootFailure), stays
     * here until power off */
    Led::flags.led_override = true;
    flags.frag_power_off = true;
    flags.frag_eject = !Fan::flags.overheated; // Not when overheated, as P2L (0xE15)
    break;

  case boot_challenge_state::retry_boot:
    /* Reset the Xbox and try again */
    PLL_Reset::flags.cold_reset_request = true;
    PLL_Reset::flags.run_after_reset = true;
    Led::flags.led_override = false;
    clearRamTestResult();
    ram_test_response0 = 0;
    ram_test_response1 = 0;
    state = boot_challenge_state::initial;
    break;

  case boot_challenge_state::check_response:
    if (flags.challenge_response_received) {
      flags.challenge_response_received = false;
      if (challenge.response0 == challenge.expected0 && challenge.response1 == challenge.expected1) {
        debug::print_message("BOOT: Challenge passed");
        challenge_check_pending = false; // As P2L (0xE49): back to waiting for a reset
        state = boot_challenge_state::challenge_wait_for_reply;
        break;
      }
      debug::print_warn("BOOT: Challenge failed, expected " + utils::toHex(challenge.expected0) + " " +
                        utils::toHex(challenge.expected1) + " got " + utils::toHex(challenge.response0) + " " +
                        utils::toHex(challenge.response1));
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
    challenge_check_pending = false;
    state = boot_challenge_state::challenge_wait_for_reply;
#endif
    break;

  case boot_challenge_state::reboot:
#if SMC_FIRMWARE_DXF
    // DXF (0x955): no power cycle, RTC dump high for 100 ticks (2 s) while the
    // system is held in reset, then try again
    hal::rtc_dump_on();
    if (--rtc_dump_time != 0) {
      return;
    }
    hal::rtc_dump_off();
    state = boot_challenge_state::retry_boot;
#else
    if (--rtc_dump_time != 0) {
      return;
    }
    if (powerCycleWithRtcDump()) {
      state = boot_challenge_state::retry_boot;
    } else {
      lockUp();
    }
#endif
    break;

  case boot_challenge_state::rtc_dump_start:
    // DXF (0x965): one tick before the RTC dump
    state = boot_challenge_state::reboot;
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

    if (bc_state < std::size(state_names)) {
        msg += std::string(" ") + state_names[bc_state];
    }

    debug::print_message(msg);
}

} // namespace BootChallenge
} // namespace SMC
