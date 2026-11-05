#include "debug.hpp"
#include "config.hpp"
#include "git_version.h"  // Generated on every build, see cmake/git_version.cmake
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "pico/time.h"

namespace debug {

static Level debugLevel = static_cast<Level>(SMC_DEBUG_LEVEL);

struct LevelInfo {
  const char* name;  // SMC_DEBUG_LEVEL value
  const char* tag;   // Shown in front of each message
};

// Indexed by Level
constexpr LevelInfo levels[] = {
    {"SILENT",   ""       },
    {"CRITICAL", "(CRIT )"},
    {"WARNINGS", "(WARN )"},
    {"INFO",     "(INFO )"},
    {"DEBUG",    "(DEBUG)"},
};
static_assert(std::size(levels) == static_cast<size_t>(Level::Debug) + 1, "One entry per Level");

static const LevelInfo& info(Level level) {
  return levels[static_cast<size_t>(level)];
}

bool enabled(Level level) {
  return level != Level::Silent && debugLevel >= level;
}

void print(Level level, const char* format, ...) {
  if (!enabled(level)) {
    return;
  }

  char    line[192];
  va_list args;
  va_start(args, format);
  const int length = vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  if (length >= static_cast<int>(sizeof(line))) {
    memcpy(line + sizeof(line) - 4, "...", 4);  // Cut off
  }

  // 12 digits, wrapping around after ~11.6 days
  constexpr uint64_t timestamp_wrap_us = 1000000000000ULL;
  printf("[%012llu] %s %s\n", to_us_since_boot(get_absolute_time()) % timestamp_wrap_us, info(level).tag, line);
  fflush(stdout);
}

const char* level_name() {
  return info(debugLevel).name;
}

Level level() {
  return debugLevel;
}

void setLevel(const Level level) {
  if (static_cast<uint8_t>(level) < level_count) {
    debugLevel = level;
  }
}

const char* levelName(const Level level) {
  return info(level).name;
}

bool parseLevel(const char* name, Level& level) {
  for (uint8_t i = 0; i < level_count; i++) {
    const char* candidate = levels[i].name;
    size_t      n = 0;
    while (name[n] != '\0' && candidate[n] != '\0' && (name[n] == candidate[n] || name[n] == candidate[n] + ('a' - 'A'))) {
      n++;
    }
    if (name[n] == '\0' && candidate[n] == '\0') {
      level = static_cast<Level>(i);
      return true;
    }
  }
  return false;
}

void print_welcome() {
  printf(
      "\n"
      "*-----------------------------------------------------------------------------*\n"
      "*                                  SMC Boot                                   *\n"
      "*-----------------------------------------------------------------------------*\n");
  fflush(stdout);
}

static const char* onOff(const bool on) {
  return on ? "ON" : "OFF";
}

void print_build_flags() {
  // Build options from CMake, see CMakeLists.txt. Those marked "default" are
  // settings: the saved ones (SETTINGS: Loaded below) replace them.
  printf("Build flags:\n");
  printf("  > GIT_HASH=%s\n", SMC_GIT_HASH);
  printf("  > PICO_BOARD=%s\n", SMC_BOARD);
  printf("  > SMC_DVD_SUPPORT=%s (default)\n", onOff(SMC_DVD_SUPPORT));
  printf("  > SMC_BOOT_CHALLENGE=%s (default)\n", onOff(SMC_BOOT_CHALLENGE));
  printf("  > SMC_AV_PACK_OVERRIDE=%s, SMC_AV_PACK_MODE=%d (default)\n", onOff(SMC_AV_PACK_OVERRIDE), SMC_AV_PACK_MODE);
  printf("  > SMC_LED_FADE=%s (default)\n", onOff(SMC_LED_FADE));
  if (SMC_NTC_SENSOR) {
    printf("  > SMC_NTC_SENSOR=ON (beta %d, R25 %d, series %d) (default)\n", SMC_NTC_BETA, SMC_NTC_R25, SMC_NTC_SERIES_R);
  } else {
    printf("  > SMC_NTC_SENSOR=OFF (default)\n");
  }
  printf("  > SMC_I2C_SNIFFER=%s (default)\n", onOff(SMC_I2C_SNIFFER));
  printf("  > SMC_RGB_LED=%s\n", onOff(SMC_RGB_LED));
  printf("  > SMC_FIRMWARE=%s\n", SMC_FIRMWARE_NAME);
  printf("  > SMC_REPORT_VERSION=%s (default)\n", SMC_VERSION_STRING);
  printf("  > SMC_DEBUG_LEVEL=%s (default)\n", level_name());
  printf("  > SMC_WEB_INTERFACE=%s\n", onOff(SMC_WEB_INTERFACE));
  fflush(stdout);
}

}  // namespace debug
