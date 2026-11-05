#include "smc_boot_challenge.hpp"
#include "smc.hpp"
#include "hal.hpp"
#include "pico/stdlib.h"

#include <iterator>
#include "text.hpp"

namespace SMC {
namespace BootChallenge {

flags_struct flags;

constexpr const char* state_names[] = {
    "Initial",    "Wait for ram test result", "Ram test ok", "Ram test failed", "Failed or no ram test result", "Wait for reply", "Boot failed", "Lock up",
    "Retry boot", "Check response",           "Reboot",      "RTC dump start"};

// Error codes readable via SMBus 0x0F
constexpr uint8_t ERROR_RAM_TEST_FAILED = 0x04;
constexpr uint8_t ERROR_CHALLENGE_FAILED = 0x01;

static challenge_struct     challenge;
static boot_challenge_state state;
static uint8_t              boot_response_timeout;
static uint8_t              failure_count;      // Failed boots since the last power cycle
static uint8_t              failure_countdown;  // Power cycles left before locking up (FRAG)
static uint8_t              rtc_dump_time;

// As P2L: the answer is only checked after a passed RAM test
static bool       challenge_check_pending;
static bool       ram_test_failed_shown = false;  // Blinking red LEDs
static uint8_t    failed_ram_chips = 0;           // decodeRamTest() of the failed test: failed or wrong size
static bool       challenge_passed = false;
static BootReport report = {};  // Not cleared by init(): it outlives the power off

void init() {
  // Same as the PIC's RAM_init
  challenge = {};
  state = boot_challenge_state::initial;
  boot_response_timeout = 0;
  failure_count = 0;
  failure_countdown = 3;
  rtc_dump_time = 0;
  challenge_check_pending = false;
  ram_test_failed_shown = false;
  failed_ram_chips = 0;
  challenge_passed = false;
}

bool ramTestFailed() {
  return ram_test_failed_shown;
}

uint8_t failedRamChips() {
  return ram_test_failed_shown ? failed_ram_chips : 0;
}

RamTest decodeRamTest(const uint8_t result, const uint8_t type) {
  RamTest test;
  // A 64 MB board has no chips 4-7: they don't answer, so all four fail. A
  // 16 MB chip or one that answers among them is a 128 MB board's.
  test.fitted = (type & 0xF0) != 0 || (result & 0xF0) != 0xF0 ? 0xFF : 0x0F;
  test.failed = result & test.fitted;
  // On a board of 16 MB chips, one that answers but aliases at R11 as an 8 MB
  // part would (none of them do on a board of 8 MB chips, type 0x00)
  test.wrong_size = type != 0x00 ? test.fitted & ~type & ~result : 0;
  test.megabytes = type == 0xFF ? 128 : 64;
  test.board_mb = test.fitted == 0xFF && type != 0x00 ? 128 : 64;  // Eight 8 MB chips make 64 MB
  test.chips_16mb = 0;
  test.chips_8mb = 0;
  for (uint8_t chip = 0; chip < 8; chip++) {
    const uint8_t bit = 1u << chip;
    if ((test.fitted & bit) && !(test.failed & bit)) {
      (type & bit ? test.chips_16mb : test.chips_8mb)++;
    }
  }
  test.found_mb = static_cast<uint16_t>(test.chips_16mb * 16 + test.chips_8mb * 8);
  return test;
}

// "BOOT: RAM found: 64 MB (4 x 16 MB)", "... 48 MB of a 64 MB board (3 x 16 MB)"
static void logRamFound(const RamTest& test) {
  Text<32> chips;
  if (test.chips_16mb != 0) {
    chips.append("%u x 16 MB", test.chips_16mb);
  }
  if (test.chips_8mb != 0) {
    chips.append(chips.empty() ? "%u x 8 MB" : ", %u x 8 MB", test.chips_8mb);
  }
  if (test.found_mb == test.board_mb) {
    debug::print(debug::Level::Info, "BOOT: RAM found: %u MB (%s)", test.found_mb, chips.c_str());
  } else {
    debug::print(debug::Level::Warnings, "BOOT: RAM found: %u MB of a %u MB board (%s)", test.found_mb, test.board_mb,
                 chips.empty() ? "no chip working" : chips.c_str());
  }
}

// "0 2" for chips 0 and 2
static Text<24> chipList(const uint8_t chips) {
  Text<24> list;
  for (uint8_t chip = 0; chip < 8; chip++) {
    if (chips & (1u << chip)) {
      list.append(list.empty() ? "%u" : " %u", chip);
    }
  }
  return list;
}

bool challengePassed() {
  return challenge_passed;
}

const BootReport& bootReport() {
  report.failed_boots = failure_count;
  return report;
}

Stage stage() {
  // As the RGB LED's phases
  if (!SMI::flags.system_running) {
    return Stage::off;
  }
  if (ram_test_failed_shown) {
    return Stage::ram_failed;
  }
  if (flags.frag_power_off) {
    return Stage::gave_up;
  }
  if (hal::system_reset_asserted()) {
    return Stage::reset;
  }
  if (challenge_passed || SMI::flags.os_ready) {
    return Stage::booted;
  }
  return flags.ram_test_submitted && flags.ram_test_passed ? Stage::kernel : Stage::bios;
}

static void reportRamTest(const bool passed) {
  report.ram_tested = true;
  report.ram_passed = passed;
  report.ram_result = ram_test_response0;
  report.ram_type = ram_test_response1;
  report.challenge = passed ? (SMC_FIRMWARE_NO_CHALLENGE ? ChallengeResult::not_checked : ChallengeResult::waiting) : ChallengeResult::none;
}

// The PIC uses its free running timers as entropy: TMR0 runs at the
// instruction clock, TMR1L at 1/8 of it
static uint8_t fastTimerByte() {
  return static_cast<uint8_t>(time_us_32());
}

static uint8_t slowTimerByte() {
  return static_cast<uint8_t>(time_us_32() >> 3);
}

// Called on every standby loop iteration. D01/DXF have no challenge
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

// Called when the Xbox has powered up, and to reseed after a reset
void seedPowerUp() {
#if SMC_FIRMWARE_NO_CHALLENGE
  return;
#endif
  challenge.input_byte2 = slowTimerByte() + challenge.input_byte0;
  challenge.input_byte3 = Fan::sensors.CPU_temperature ^ challenge.input_byte1;
}

void compute() {
  // Same as the PIC
  challenge.expected0 = 0x33;
  challenge.expected1 = 0xED;

  for (uint8_t i = 0; i < 4; i++) {
    challenge.expected0 = (static_cast<uint8_t>(challenge.input_byte0 << 2) ^ static_cast<uint8_t>(challenge.input_byte1 + 0x39) ^
                           static_cast<uint8_t>(challenge.input_byte2 >> 2) ^ static_cast<uint8_t>(challenge.input_byte3 + 0x63) ^ challenge.expected1) +
                          challenge.expected0;
    challenge.expected1 = (static_cast<uint8_t>(challenge.input_byte0 + 0x0B) ^ static_cast<uint8_t>(challenge.input_byte1 >> 2) ^
                           static_cast<uint8_t>(challenge.input_byte2 + 0x1B) ^ challenge.expected0) +
                          challenge.expected1;
  }
}

static void clearRamTestResult() {
  flags.ram_test_submitted = false;
  flags.ram_test_passed = false;
}

// As P2L: new challenge on every reset, and the power cycle count
// starts again. D01/DXF have no challenge and leave the bytes alone.
static void newChallengeAfterReset() {
  clearRamTestResult();
  challenge_passed = false;
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

#if !SMC_FIRMWARE_DXF  // DXF only pulses RTC dump, without a power cycle
// Power cycles the Xbox while dumping the RTC, as the PIC does after repeated
// boot failures. Blocks the main loop like the original.
static bool powerCycleWithRtcDump() {
  const auto tick = [] {
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

static bool enforce = SMC_BOOT_CHALLENGE;

bool enforced() {
  return enforce;
}

void setEnforced(const bool on) {
  enforce = on;
}

// Follows P2L for every firmware. Not enforced, the state machine
// still runs to log the results, but a failure never resets the Xbox or locks
// it up: it carries on, then waits in challenge_wait_for_reply for the next
// reset
void update() {
  switch (state) {
    case boot_challenge_state::initial:
      boot_response_timeout = 15;
      PLL_Reset::flags.run_after_reset = true;
      Dvd::restartTrayTracking();
#if !SMC_FIRMWARE_NO_CHALLENGE
      compute();  // D01/DXF skip it
#endif
      state = boot_challenge_state::wait_for_ram_test_result;
      break;

    case boot_challenge_state::wait_for_ram_test_result:
      if (flags.ram_test_submitted) {
        if (flags.ram_test_passed) {
          state = boot_challenge_state::ram_test_ok;
        } else {
          state = boot_challenge_state::ram_test_failed;
        }
      } else if (--boot_response_timeout == 0) {
        debug::print(debug::Level::Warnings, "BOOT: No RAM test result");
        state = enforce ? boot_challenge_state::challenge_failed_or_no_ram_test_result : boot_challenge_state::challenge_wait_for_reply;
      }
      break;

    case boot_challenge_state::ram_test_ok:
      debug::print(debug::Level::Info, "BOOT: RAM test passed, result 0x%02X type 0x%02X", ram_test_response0, ram_test_response1);
      logRamFound(decodeRamTest(ram_test_response0, ram_test_response1));
      reportRamTest(true);
      ram_test_failed_shown = false;
      failed_ram_chips = 0;
      challenge_check_pending = !SMC_FIRMWARE_NO_CHALLENGE;  // D01/DXF only check the RAM test
      state = boot_challenge_state::challenge_wait_for_reply;
      break;

    case boot_challenge_state::ram_test_failed: {
      // Which chips, see decodeRamTest(). Kept, as a retry clears the result.
      const RamTest test = decodeRamTest(ram_test_response0, ram_test_response1);
      failed_ram_chips = test.failed | test.wrong_size;
      Text<96> chips;
      if (test.failed != 0) {
        chips.append(", chip(s) %s failed or missing", chipList(test.failed).c_str());
      }
      if (test.wrong_size != 0) {
        chips.append(", chip(s) %s read as 8 MB (address line R11)", chipList(test.wrong_size).c_str());
      }
      debug::print(debug::Level::Warnings, "BOOT: RAM test failed, result 0x%02X type 0x%02X, %s board%s", ram_test_response0, ram_test_response1,
                   test.board_mb == 128 ? "128 MB" : "64 MB", chips.empty() ? ", no chip flagged" : chips.c_str());
      logRamFound(test);
      reportRamTest(false);

      // Blinking red, only for a failed RAM test (the PIC shows red/orange, 0x0F
      // red and 0x05 green, and only when it enforces the boot challenge). Shown
      // on top of everything until a RAM test passes or the Xbox powers off.
      Led::setBootFailurePattern(0x0A, 0x00);
      ram_test_failed_shown = true;
      if (enforce) {
        Led::setRedPhases(0x0A);
        Led::setGreenPhases(0x00);
        bios_response_byte0 = ERROR_RAM_TEST_FAILED;
        state = boot_challenge_state::boot_failed;
      } else {
        state = boot_challenge_state::challenge_wait_for_reply;
      }
      break;
    }

    case boot_challenge_state::challenge_failed_or_no_ram_test_result:
      Led::setRedPhases(0x0A);
      Led::setGreenPhases(0x05);
      Led::setBootFailurePattern(0x0A, 0x05);
      bios_response_byte0 = ERROR_CHALLENGE_FAILED;
      state = boot_challenge_state::boot_failed;
      break;

    case boot_challenge_state::challenge_wait_for_reply:
      // As P2L: check the answer once after a passed RAM test, otherwise
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
      AudioClamp::flags.audio_clamp_request = true;  // Also mutes the audio, as P2L
      failure_count++;
      if (failure_count <= 3) {
        state = boot_challenge_state::retry_boot;
      } else if (--failure_countdown != 0) {
        failure_count = 0;
        PLL_Reset::flags.cold_reset_request = true;
        PLL_Reset::flags.run_after_reset = false;
#if SMC_FIRMWARE_DXF
        rtc_dump_time = 100;  // DXF: RTC dump while held in reset
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
      // FRAG: error code LEDs (top priority, see Led::setBootFailure), stays
      // here until power off
      Led::flags.led_override = true;
      flags.frag_power_off = true;
      flags.frag_eject = !Fan::flags.overheated;  // Not when overheated, as P2L
      break;

    case boot_challenge_state::retry_boot:
      // Reset the Xbox and try again
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
          debug::print(debug::Level::Info, "BOOT: Challenge passed");
          challenge_passed = true;
          report.challenge = ChallengeResult::passed;
          challenge_check_pending = false;  // As P2L: back to waiting for a reset
          state = boot_challenge_state::challenge_wait_for_reply;
          break;
        }
        debug::print(debug::Level::Warnings, "BOOT: Challenge failed, expected 0x%02X 0x%02X got 0x%02X 0x%02X", challenge.expected0, challenge.expected1,
                     challenge.response0, challenge.response1);
        report.challenge = ChallengeResult::wrong;
      } else if (--boot_response_timeout != 0) {
        break;
      } else {
        debug::print(debug::Level::Warnings, "BOOT: No challenge answer");
        report.challenge = ChallengeResult::no_answer;
      }

      if (enforce) {
        // Wrong answer or timeout: go straight to a power cycle
        failure_count = 4;
        state = boot_challenge_state::challenge_failed_or_no_ram_test_result;
      } else {
        challenge_check_pending = false;
        state = boot_challenge_state::challenge_wait_for_reply;
      }
      break;

    case boot_challenge_state::reboot:
#if SMC_FIRMWARE_DXF
      // DXF: no power cycle, RTC dump high for 100 ticks (2 s) while the
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
      // DXF: one tick before the RTC dump
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
  debug::print(debug::Level::Info, "Boot challenge state [%u] %s", static_cast<unsigned>(bc_state),
               bc_state < std::size(state_names) ? state_names[bc_state] : "");
}

}  // namespace BootChallenge
}  // namespace SMC
