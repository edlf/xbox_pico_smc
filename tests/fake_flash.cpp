// Fake flash for src/smc_settings.cpp: an array in RAM. The settings find the
// end of the firmware from __flash_binary_end (a linker symbol on the Pico);
// here that symbol is the array itself, so the firmware takes no space.

#include "fakes.hpp"
#include "hardware/flash.h"
#include "pico/flash.h"

#include <cstring>

extern "C" {
alignas(4) uint8_t __flash_binary_end[PICO_FLASH_SIZE_BYTES];
}

uintptr_t fake_xip_base() {
  return reinterpret_cast<uintptr_t>(__flash_binary_end);
}

void flash_range_erase(uint32_t offset, size_t count) {
  std::memset(__flash_binary_end + offset, 0xFF, count);
}

void flash_range_program(uint32_t offset, const uint8_t* data, size_t count) {
  for (size_t i = 0; i < count; i++) {
    __flash_binary_end[offset + i] &= data[i];  // Programming only clears bits
  }
}

int flash_safe_execute(void (*func)(void*), void* param, uint32_t) {
  func(param);
  fake::flash_writes++;
  return PICO_OK;
}

namespace fake {

int flash_writes = 0;

uint8_t* flash() {
  return __flash_binary_end;
}

void eraseFlash() {
  std::memset(__flash_binary_end, 0xFF, sizeof(__flash_binary_end));
  flash_writes = 0;
}

}  // namespace fake
