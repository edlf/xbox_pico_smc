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

// AV pack ID read from the AV connector's three mode pins (PORTB & 0x0E on the
// PIC). SMBus 0x04 reports it shifted down by one (the code in the comments).
enum class av_pack : uint8_t {
  scart = 0x00,        // 0 Advanced SCART cable
  hdtv = 0x02,         // 1 High Definition AV pack
  vga = 0x04,          // 2 VGA / progressive RGB
  rf = 0x06,           // 3 RF adapter
  advanced = 0x08,     // 4 Advanced AV pack (S-Video)
  special = 0x0A,      // 5 No known pack. With no cable in standby the pins read
                       //   this (mode 1 is pulled up to POWON); while unplugged
                       //   it acts as a power button press
  standard = 0x0C,     // 6 Standard AV cable
  none = 0x0E          // 7 No cable
};

struct sensors_struct {
  av_pack vmode; // Debounced AV pack, SMBus 0x04
};
extern sensors_struct sensors;

// AV flags, from the PIC's RAM (address.bit)
struct flags_struct {
  bool av_cable_missing; // 71.6 LEDs and audio clamp
};
extern flags_struct flags;

void init();
void update();
void printMode(const av_pack vm);

} // namespace Video
} // namespace SMC

#endif // SMC_VIDEO_HPP
