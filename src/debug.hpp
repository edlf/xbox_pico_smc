#ifndef DEBUG_HPP
#define DEBUG_HPP

namespace debug {

// Ordered by verbosity: a level prints its own messages and everything above it
enum class Level { Silent, Critical, Warnings, Info, Debug };

bool enabled(Level level);

// Timestamp, level tag and the message (printf format), if the level is
// enabled. Formatted into a fixed buffer, no heap: longer lines end in "...".
__attribute__((format(printf, 2, 3))) void print(Level level, const char* format, ...);

const char* level_name();
void        print_welcome();
void        print_build_flags();

}  // namespace debug

#endif  // DEBUG_HPP
