#include "debug.hpp"
#include "config.hpp"
#include <iostream>
#include <iterator>
#include "pico/time.h"

namespace debug {

DebugLevel debugLevel = static_cast<DebugLevel>(SMC_DEBUG_LEVEL);

struct LevelInfo {
  const char* name; // SMC_DEBUG_LEVEL value
  const char* tag;  // Shown in front of each message
};

// Indexed by DebugLevel
constexpr LevelInfo levels[] = {
    {"SILENT",   ""},
    {"CRITICAL", "(CRIT )"},
    {"WARNINGS", "(WARN )"},
    {"INFO",     "(INFO )"},
    {"DEBUG",    "(DEBUG)"},
};
static_assert(std::size(levels) == static_cast<size_t>(DebugLevel::Debug) + 1, "One entry per DebugLevel");

const LevelInfo& info(DebugLevel level) {
  return levels[static_cast<size_t>(level)];
}

bool enabled(DebugLevel level) {
  return level != DebugLevel::Silent && debugLevel >= level;
}

void print(DebugLevel level, const std::string& msg) {
  if (!enabled(level)) {
    return;
  }

  // 12 digits, wrapping around after ~11.6 days
  constexpr uint64_t timestamp_wrap_us = 1000000000000ULL;
  char timestamp[sizeof("[000000000000] ")];
  snprintf(timestamp, sizeof(timestamp), "[%012llu] ", to_us_since_boot(get_absolute_time()) % timestamp_wrap_us);

  std::cout << timestamp << info(level).tag << " " << msg << std::endl;
}

const char* level_name() {
  return info(debugLevel).name;
}

void print_welcome() {
  std::cout << std::endl
            << "*-----------------------------------------------------------------------------*\n"
            << "*                                  SMC Boot                                   *\n"
            << "*-----------------------------------------------------------------------------*"
            << std::endl;
}

void print_build_flags(){
  // Build options from CMake, see CMakeLists.txt
  std::cout << std::string("Build flags:") +
      "\n  > PICO_BOARD=" + SMC_BOARD +
      "\n  > SMC_DVD_SUPPORT=" + (SMC_DVD_SUPPORT ? "ON" : "OFF") +
      "\n  > SMC_BOOT_CHALLENGE=" + (SMC_BOOT_CHALLENGE ? "ON" : "OFF") +
      "\n  > SMC_AV_PACK_OVERRIDE=" + (SMC_AV_PACK_OVERRIDE ? "ON" : "OFF") +
      "\n  > SMC_AV_PACK_MODE=" + std::to_string(SMC_AV_PACK_MODE) +
      "\n  > SMC_LED_BRIGHTNESS=" + std::to_string(SMC_LED_BRIGHTNESS) +
      "\n  > SMC_LED_FADE=" + (SMC_LED_FADE ? "ON" : "OFF") +
      "\n  > SMC_NTC_SENSOR=" + (SMC_NTC_SENSOR ? "ON (beta " + std::to_string(SMC_NTC_BETA) +
                                 ", R25 " + std::to_string(SMC_NTC_R25) + ", series " +
                                 std::to_string(SMC_NTC_SERIES_R) + ")" : "OFF") +
      "\n  > SMC_FIRMWARE=" + SMC_FIRMWARE_NAME +
      "\n  > SMC_REPORT_VERSION=" + SMC_VERSION_STRING +
      "\n  > SMC_DEBUG_LEVEL=" + debug::level_name() +
      "\n  > SMC_WEB_INTERFACE=" + (SMC_WEB_INTERFACE ? "ON" : "OFF") << std::endl;
}

} // namespace debug
