#ifndef SMC_VIDEO_HPP
#define SMC_VIDEO_HPP

#include <cstdint>

// Set by CMake (options SMC_AV_PACK_OVERRIDE, SMC_AV_PACK_MODE): report a fixed
// AV pack code instead of the one read from the AV connector
#ifndef SMC_AV_PACK_OVERRIDE
#define SMC_AV_PACK_OVERRIDE 0
#endif
#ifndef SMC_AV_PACK_MODE
#define SMC_AV_PACK_MODE 1 // High Definition AV Pack
#endif

namespace SMC {
namespace Video {

void init();
void update();
void printMode(const uint8_t vm);

} // namespace Video
} // namespace SMC

#endif // SMC_VIDEO_HPP
