#ifndef DEBUG_HPP
#define DEBUG_HPP

#include <cstdint>

namespace debug {

// Ordered by verbosity: a level prints its own messages and everything above it
enum class Level { Silent, Critical, Warnings, Info, Debug };

bool enabled(Level level);

// Timestamp, level tag and the message (printf format), if the level is
// enabled. Formatted into a fixed buffer, no heap: longer lines end in "...".
__attribute__((format(printf, 2, 3))) void print(Level level, const char* format, ...);

// The level messages are printed at: saved setting, SMC_DEBUG_LEVEL by default
Level             level();
void              setLevel(Level level);
constexpr uint8_t level_count = static_cast<uint8_t>(Level::Debug) + 1;
const char*       levelName(Level level);  // "SILENT" ... "DEBUG"
// Upper or lower case. False (and level unchanged) for anything else.
bool parseLevel(const char* name, Level& level);
// A line as it is, without the timestamp and level: the boot sections (build
// options, settings)
__attribute__((format(printf, 1, 2))) void line(const char* format, ...);

void print_welcome();
void print_build_flags();

}  // namespace debug

#endif  // DEBUG_HPP
