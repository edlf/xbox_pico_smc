#include "debug.hpp"
#include <iostream>
#include <iterator>
#include "pico/time.h"

namespace debug {

// Set by CMake (SMC_DEBUG_LEVEL), the index into DebugLevel
#ifndef SMC_DEBUG_LEVEL
#define SMC_DEBUG_LEVEL 3 // Info
#endif
DebugLevel debugLevel = static_cast<DebugLevel>(SMC_DEBUG_LEVEL);

struct LevelInfo {
  const char* name; // SMC_DEBUG_LEVEL value
  const char* tag;  // Shown in front of each message
};

// Indexed by DebugLevel
constexpr LevelInfo levels[] = {
    {"SILENT", ""},
    {"CRITICAL", "(CRIT )"},
    {"WARNINGS", "(WARN )"},
    {"INFO", "(INFO )"},
    {"DEBUG", "(DEBUG)"},
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
  // [microseconds since boot] (TAG  ) message
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
  std::cout << std::endl;
  std::cout << "*-----------------------------------------------------------------------------*\n"
            << "*                                  SMC Boot                                   *\n"
            << "*-----------------------------------------------------------------------------*"
            << std::endl;
}

} // namespace debug
