// debug:: that records each line in fake::log instead of printing it.
// Every level is kept, DEBUG included, so tests can check any message.

#include "debug.hpp"
#include "fakes.hpp"

namespace debug {

static const char* tag(DebugLevel level) {
  switch (level) {
    case DebugLevel::Critical:
      return "(CRIT ) ";
    case DebugLevel::Warnings:
      return "(WARN ) ";
    case DebugLevel::Info:
      return "(INFO ) ";
    case DebugLevel::Debug:
      return "(DEBUG) ";
    default:
      return "";
  }
}

bool enabled(DebugLevel level) {
  return level != DebugLevel::Silent;
}

void print(DebugLevel level, const std::string& msg) {
  fake::log.push_back(tag(level) + msg);
}

const char* level_name() {
  return "DEBUG";
}

void print_welcome() {}
void print_build_flags() {}

}  // namespace debug
