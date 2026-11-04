#ifndef FAKE_HARDWARE_FLASH_H
#define FAKE_HARDWARE_FLASH_H

// A small flash in RAM (fake_flash.cpp) for the settings tests. XIP_BASE is
// where it starts, and the firmware "ends" there too, so the last sector is free.

#include <cstddef>
#include <cstdint>

#define FLASH_PAGE_SIZE 256u
#define FLASH_SECTOR_SIZE 4096u
#define PICO_FLASH_SIZE_BYTES (2u * FLASH_SECTOR_SIZE)

uintptr_t fake_xip_base();
#define XIP_BASE (fake_xip_base())

void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t* data, size_t count);

#endif
