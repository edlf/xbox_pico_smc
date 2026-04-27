#ifndef __SMC_VIDEO__
#define __SMC_VIDEO__

#include <cstdint>

namespace SMC {
namespace Video {

void init();
void update();
void printMode(const uint8_t vm);

} // namespace Video
} // namespace SMC

#endif // __SMC_VIDEO__
