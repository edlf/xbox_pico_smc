// debug:: that records each line in fake::log instead of printing it.
// Every level is kept, DEBUG included, so tests can check any message.

#include "debug.hpp"
#include "config.hpp"
#include "fakes.hpp"

#include <cstdarg>
#include <cstdio>
#include <string>

namespace debug {

static const char* tag(Level level) {
  switch (level) {
    case Level::Critical:
      return "(CRIT ) ";
    case Level::Warnings:
      return "(WARN ) ";
    case Level::Info:
      return "(INFO ) ";
    case Level::Debug:
      return "(DEBUG) ";
    default:
      return "";
  }
}

bool enabled(Level level) {
  return level != Level::Silent;
}

void print(Level level, const char* format, ...) {
  char    line[512];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  fake::log.push_back(std::string(tag(level)) + line);
  if (fake::echo_log && (level != Level::Debug || fake::echo_debug)) {
    printf("[%012llu] %s\n", static_cast<unsigned long long>(fake::now_us()), fake::log.back().c_str());
  }
}

// Kept, so settings can be tested; enabled() still lets every line through
static Level current_level = static_cast<Level>(SMC_DEBUG_LEVEL);

Level level() {
  return current_level;
}

void setLevel(Level level) {
  if (static_cast<uint8_t>(level) < level_count) {
    current_level = level;
  }
}

const char* levelName(Level level) {
  static const char* const names[] = {"SILENT", "CRITICAL", "WARNINGS", "INFO", "DEBUG"};
  return names[static_cast<uint8_t>(level)];
}

bool parseLevel(const char* name, Level& level) {
  for (uint8_t i = 0; i < level_count; i++) {
    const char* candidate = levelName(static_cast<Level>(i));
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

void line(const char* format, ...) {
  char    text[512];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  fake::log.push_back(text);
  if (fake::echo_log) {
    printf("%s\n", text);
  }
}

void print_welcome() {}
void print_build_flags() {}

}  // namespace debug
