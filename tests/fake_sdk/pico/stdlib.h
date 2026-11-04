#ifndef FAKE_PICO_STDLIB_H
#define FAKE_PICO_STDLIB_H

// The few Pico SDK names the SMC modules use, for the host tests. Time is a
// counter the tests advance (fake_sdk.cpp), waits only move it on.

#include <cstddef>
#include <cstdint>

typedef unsigned int uint;

uint32_t time_us_32();
uint64_t time_us_64();
void     busy_wait_us(uint32_t us);
void     busy_wait_ms(uint32_t ms);

#define GPIO_IRQ_EDGE_FALL 0x4u
typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t events);
void gpio_acknowledge_irq(uint gpio, uint32_t events);
void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool enabled, gpio_irq_callback_t callback);

#endif
