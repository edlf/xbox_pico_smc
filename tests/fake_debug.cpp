// debug:: that records each line in fake::log instead of printing it.
// Every level is kept, DEBUG included, so tests can check any message.

#include "debug.hpp"
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
}

const char* level_name() {
  return "DEBUG";
}

void print_welcome() {}
void print_build_flags() {}

}  // namespace debug
