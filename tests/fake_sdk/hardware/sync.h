#ifndef FAKE_HARDWARE_SYNC_H
#define FAKE_HARDWARE_SYNC_H
#include <cstdint>
inline uint32_t save_and_disable_interrupts() {
  return 0;
}
inline void restore_interrupts(uint32_t) {}
#endif
