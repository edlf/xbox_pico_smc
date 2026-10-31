#ifndef DEBUG_HPP
#define DEBUG_HPP

#include <string>

namespace debug {

// Ordered by verbosity: a level prints its own messages and everything above it
enum class DebugLevel {
  Silent,   // Nothing
  Critical, // print_critical
  Warnings, // + print_warn
  Info,     // + print_message (default)
  Debug     // + print_debug (SMBus transactions, DVD tray states)
};

void print_critical(const std::string&);
void print_warn(const std::string&);
void print_message(const std::string&);
void print_debug(const std::string&);
// False below DebugLevel::Debug, to skip building messages nobody sees
bool debug_enabled();
const char* level_name();
void print_welcome();

} // namespace debug

#endif // DEBUG_HPP
