#ifndef FAKE_PICO_FLASH_H
#define FAKE_PICO_FLASH_H

#include <cstdint>

#ifndef PICO_OK
#define PICO_OK 0
#endif

// Runs func at once (there's no other core or interrupt to stop)
int flash_safe_execute(void (*func)(void*), void* param, uint32_t enter_exit_timeout_ms);

#endif
