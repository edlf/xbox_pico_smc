#include "smc_i2c_sniffer.hpp"
#include "smc_i2c_decoder.hpp"
#include "smc.hpp"
#include "pin_assignments.hpp"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "pico/time.h"

#include <iterator>

namespace SMC {
namespace I2CSniffer {

#if SMC_I2C_SNIFFER

// Capture: a PIO state machine watches SDA and SCL (it only reads the pins,
// they stay with the I2C slave) and records their levels each time either
// changes, 2 bits per change (bit 0 SDA, bit 1 SCL), 16 changes per word,
// oldest in the low bits. DMA copies the words into a ring buffer, and the main
// loop hands them to the decoder (smc_i2c_decoder.cpp).
//
//   changed:  mov y, x        ; new state
//             in y, 2         ; record it (autopush every 16)
//   .wrap_target
//             mov osr, pins   ; SDA, SCL, ...
//             out x, 2        ; x = SCL:SDA
//             jmp x!=y changed
//   .wrap
static_assert(pins::I2C_SCL == pins::I2C_SDA + 1, "The PIO reads SDA and SCL as two adjacent pins");

static const uint16_t sniff_instructions[] = {
    static_cast<uint16_t>(pio_encode_mov(pio_y, pio_x)),      static_cast<uint16_t>(pio_encode_in(pio_y, 2)),
    static_cast<uint16_t>(pio_encode_mov(pio_osr, pio_pins)), static_cast<uint16_t>(pio_encode_out(pio_x, 2)),
    static_cast<uint16_t>(pio_encode_jmp_x_ne_y(0)),
};
static pio_program_t sniff_program = {};  // Filled in by init()
constexpr uint       wrap_target = 2;
constexpr uint       wrap = 4;

// ~100 ms of a busy 100 kHz bus (27 changes per byte), decoded every 20 ms
constexpr uint ring_bits = 13;  // 8 KB
constexpr uint ring_words = (1u << ring_bits) / 4;
alignas(1u << ring_bits) static uint32_t ring[ring_words];

static PIO  pio;
static uint sm;
static int  dma_channel = -1;
static uint read_index = 0;

static uint writeIndex() {
  return (dma_channel_hw_addr(dma_channel)->write_addr - reinterpret_cast<uintptr_t>(ring)) / 4;
}

static void decodeCaptured() {
  const uint end = writeIndex();
  while (read_index != end) {
    uint32_t word = ring[read_index];
    read_index = (read_index + 1) % ring_words;
    for (uint8_t i = 0; i < 16; i++) {
      I2CDecoder::sample(static_cast<uint8_t>(word & 0x3));
      word >>= 2;
    }
  }
}

// Push the changes still waiting in the PIO's input shift register: record
// the current state 16 more times (repeats are ignored by the decoder), then
// wait for the DMA to copy them
static void flush() {
  for (uint8_t i = 0; i < 16; i++) {
    pio_sm_exec(pio, sm, pio_encode_in(pio_y, 2));
  }
  const absolute_time_t deadline = make_timeout_time_us(100);
  while (!pio_sm_is_rx_fifo_empty(pio, sm) && !time_reached(deadline)) {
  }
  busy_wait_us(1);  // Last word on its way to the ring
}

void init() {
  sniff_program.instructions = sniff_instructions;
  sniff_program.length = std::size(sniff_instructions);
  sniff_program.origin = -1;  // Anywhere, the jmp is relocated

  uint offset;
  if (!pio_claim_free_sm_and_add_program(&sniff_program, &pio, &sm, &offset)) {
    debug::print(debug::Level::Warnings, "ENCODER: No free PIO state machine, sniffer off");
    return;
  }
  pio_sm_config c = pio_get_default_sm_config();
  sm_config_set_wrap(&c, offset + wrap_target, offset + wrap);
  sm_config_set_in_pins(&c, pins::I2C_SDA);      // Not pio_gpio_init: the pins stay with the I2C slave
  sm_config_set_in_shift(&c, true, true, 32);    // Right, autopush every 16 changes
  sm_config_set_out_shift(&c, true, false, 32);  // Right, x takes the 2 low bits
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  // Sample every ~200 ns (3 instructions at 15 MHz): fine enough for 100 kHz
  // SMBus timing (300 ns data hold), coarse enough to ride out ringing on the
  // slow edges
  sm_config_set_clkdiv(&c, static_cast<float>(clock_get_hz(clk_sys)) / 15000000.0f);
  pio_sm_init(pio, sm, offset + wrap_target, &c);

  dma_channel = dma_claim_unused_channel(false);
  if (dma_channel < 0) {
    debug::print(debug::Level::Warnings, "ENCODER: No free DMA channel, sniffer off");
    pio_remove_program_and_unclaim_sm(&sniff_program, pio, sm, offset);
    return;
  }
  dma_channel_config d = dma_channel_get_default_config(dma_channel);
  channel_config_set_transfer_data_size(&d, DMA_SIZE_32);
  channel_config_set_read_increment(&d, false);
  channel_config_set_write_increment(&d, true);
  channel_config_set_ring(&d, true, ring_bits);  // Wrap the writes around the ring
  channel_config_set_dreq(&d, pio_get_dreq(pio, sm, false));
  // RP2350: endless. RP2040 has no endless mode, this is 4 billion words
  // (60 hours of a constantly busy bus).
  dma_channel_configure(dma_channel, &d, ring, &pio->rxf[sm], 0xFFFFFFFFu, true);

  read_index = 0;
  pio_sm_set_enabled(pio, sm, true);
  debug::print(debug::Level::Info, "ENCODER: Watching the SMBus for the video encoder");
}

void update() {
  if (dma_channel < 0) {
    return;
  }
  // With the bus idle nothing new is coming, push out the last changes
  if (gpio_get(pins::I2C_SDA) && gpio_get(pins::I2C_SCL)) {
    flush();
  }
  decodeCaptured();
  I2CDecoder::report();
}

void resetFound() {
  I2CDecoder::resetFound();
}

void beginOwnTransfer() {
  if (dma_channel < 0) {
    return;
  }
  flush();
  decodeCaptured();  // Up to here it's the CPU's
  I2CDecoder::setIgnoring(true);
}

void endOwnTransfer() {
  if (dma_channel < 0) {
    return;
  }
  flush();
  decodeCaptured();  // Ours, not logged
  I2CDecoder::setIgnoring(false);
}

#else

void init() {}
void update() {}
void resetFound() {}
void beginOwnTransfer() {}
void endOwnTransfer() {}

#endif  // SMC_I2C_SNIFFER

}  // namespace I2CSniffer
}  // namespace SMC
