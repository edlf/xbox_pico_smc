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

// True when messages of this level are printed, to skip building messages
// nobody sees
bool enabled(DebugLevel level);
// Timestamp, level tag and the message, if the level is enabled
void print(DebugLevel level, const std::string& msg);

inline void print_critical(const std::string& msg) { print(DebugLevel::Critical, msg); }
inline void print_warn(const std::string& msg) { print(DebugLevel::Warnings, msg); }
inline void print_message(const std::string& msg) { print(DebugLevel::Info, msg); }
inline void print_debug(const std::string& msg) { print(DebugLevel::Debug, msg); }
inline bool debug_enabled() { return enabled(DebugLevel::Debug); }

const char* level_name();
void print_welcome();

} // namespace debug

#endif // DEBUG_HPP
