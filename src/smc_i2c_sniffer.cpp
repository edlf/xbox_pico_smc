#include "smc_i2c_sniffer.hpp"
#include "smc_encoder_mode.hpp"
#include "smc.hpp"
#include "pin_assignments.hpp"
#include "utils.hpp"
#include "ring_buffer.hpp"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "pico/time.h"

#include <iterator>
#include <string>

namespace SMC {
namespace I2CSniffer {

#if SMC_I2C_SNIFFER

// Capture: a PIO state machine watches SDA and SCL (it only reads the pins,
// they stay with the I2C slave) and records their levels each time either
// changes, 2 bits per change (bit 0 SDA, bit 1 SCL), 16 changes per word,
// oldest in the low bits. DMA copies the words into a ring buffer, decoded
// from the main loop.
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
    (uint16_t)pio_encode_mov(pio_y, pio_x), (uint16_t)pio_encode_in(pio_y, 2),  (uint16_t)pio_encode_mov(pio_osr, pio_pins),
    (uint16_t)pio_encode_out(pio_x, 2),     (uint16_t)pio_encode_jmp_x_ne_y(0),
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

// Video encoders on the SMBus (7-bit addresses)
struct Encoder {
  uint8_t     address;
  const char* name;
};
constexpr Encoder encoders[] = {
    {0x45, "Conexant"}, // 1.0-1.3
    {0x6A, "Focus"   }, // 1.4-1.5
    {0x70, "Xcalibur"}, // 1.6
};

static int8_t encoderIndex(const uint8_t address) {
  for (uint8_t i = 0; i < std::size(encoders); i++) {
    if (encoders[i].address == address) {
      return i;
    }
  }
  return -1;
}

static const char* encoderName(const uint8_t address) {
  const int8_t i = encoderIndex(address);
  return i < 0 ? nullptr : encoders[i].name;
}

// Encoders that acknowledged their address since power on (the BIOS probes
// each one, only the board's own answers), one bit per encoders[] entry
static uint8_t found_mask = 0;
static uint8_t logged_mask = 0;

// Where the decoder is in the bus traffic
constexpr uint8_t max_bytes = 6;  // Address, register and up to 4 data bytes kept
struct Decoder {
  uint8_t prev_state = 0x3;     // Idle: SDA and SCL high
  bool    in_transfer = false;  // Between START and STOP
  bool    restarted = false;    // A repeated START in this transfer: a read, not a write
  // The current segment (after a START)
  uint8_t bit_count = 0;  // Bits of the current byte, the 9th is the ACK
  uint8_t current_byte = 0;
  uint8_t bytes[max_bytes] = {};
  uint8_t byte_count = 0;  // Bytes in this segment (more than max_bytes are counted, not kept)
  bool    nacked = false;  // A byte of this segment wasn't acknowledged
};
static Decoder decoder;
static bool    ignoring = false;  // During our own master transfer

// Writes waiting to be printed. Printing is slow (~4 ms a line at 115200
// baud), so only a couple per tick: the BIOS writes the encoder in bursts.
struct LogEntry {
  uint8_t address;
  uint8_t count;  // Bytes after the address (register and data)
  uint8_t data[max_bytes - 1];
  bool    nacked;
};
constexpr uint8_t                prints_per_tick = 2;
static RingBuffer<LogEntry, 128> writes;

static void segmentStart() {
  decoder.bit_count = 0;
  decoder.current_byte = 0;
  decoder.byte_count = 0;
  decoder.nacked = false;
}

// A write ended by STOP, to an encoder, with a register and at least one byte
static void segmentEnd() {
  if (!debug::debug_enabled() || ignoring || decoder.restarted || decoder.byte_count < 3 || (decoder.bytes[0] & 1) != 0) {
    return;
  }
  const uint8_t address = decoder.bytes[0] >> 1;
  if (encoderName(address) == nullptr) {
    return;
  }
  LogEntry entry = {};
  entry.address = address;
  entry.count = decoder.byte_count - 1;
  for (uint8_t i = 1; i < decoder.byte_count && i < max_bytes; i++) {
    entry.data[i - 1] = decoder.bytes[i];
  }
  entry.nacked = decoder.nacked;
  writes.push(entry);
}

static void decodeSample(const uint8_t state) {
  if (state == decoder.prev_state) {
    return;
  }
  const bool sda = state & 1;
  const bool scl = state & 2;
  const bool prev_scl = decoder.prev_state & 2;
  decoder.prev_state = state;

  if (prev_scl && scl) {
    // SDA changed with SCL high
    if (!sda) {  // START or repeated START
      decoder.restarted = decoder.in_transfer;
      decoder.in_transfer = true;
      segmentStart();
    } else if (decoder.in_transfer) {  // STOP
      segmentEnd();
      decoder.in_transfer = false;
    }
    return;
  }

  if (!prev_scl && scl && decoder.in_transfer) {
    // SCL rising: a data bit, or the ACK after 8 of them
    if (decoder.bit_count < 8) {
      decoder.current_byte = (decoder.current_byte << 1) | sda;
      decoder.bit_count++;
    } else {
      if (decoder.byte_count < max_bytes) {
        decoder.bytes[decoder.byte_count] = decoder.current_byte;
      }
      // Each data byte the CPU writes to an encoder or an X-HD (0x69, its mode
      // packet), register byte at index 1
      if (decoder.byte_count >= 2 && !ignoring && (decoder.bytes[0] & 1) == 0 &&
          (encoderIndex(decoder.bytes[0] >> 1) >= 0 || (decoder.bytes[0] >> 1) == 0x69)) {
        EncoderMode::onWrite(decoder.bytes[0] >> 1, decoder.bytes[1], decoder.byte_count - 2, decoder.current_byte);
      }
      if (decoder.byte_count < 255) {
        decoder.byte_count++;
      }
      decoder.nacked |= sda;  // SDA high: not acknowledged
      if (decoder.byte_count == 1 && !sda && !ignoring) {
        // An encoder answered its address: it's on this board
        const int8_t i = encoderIndex(decoder.current_byte >> 1);
        if (i >= 0) {
          found_mask |= 1u << i;
        }
      }
      decoder.bit_count = 0;
      decoder.current_byte = 0;
    }
  }
}

static uint writeIndex() {
  return (dma_channel_hw_addr(dma_channel)->write_addr - (uintptr_t)ring) / 4;
}

static void decodeCaptured() {
  const uint end = writeIndex();
  while (read_index != end) {
    uint32_t word = ring[read_index];
    read_index = (read_index + 1) % ring_words;
    for (uint8_t i = 0; i < 16; i++) {
      decodeSample(word & 0x3);
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

static void printFound() {
  const uint8_t new_found = found_mask & ~logged_mask;
  for (uint8_t i = 0; i < std::size(encoders); i++) {
    if (new_found & (1u << i)) {
      debug::print_message(std::string("ENCODER: Found ") + encoders[i].name + " (" + utils::toHex(encoders[i].address) + ")");
    }
  }
  logged_mask |= new_found;
}

static void printWrites() {
  LogEntry entry;
  for (uint8_t n = 0; n < prints_per_tick && writes.pop(entry); n++) {
    std::string   msg = std::string("ENCODER: ") + encoderName(entry.address) + " [" + utils::toHex(entry.data[0]) + "] =";
    const uint8_t kept = entry.count < max_bytes - 1 ? entry.count : max_bytes - 1;
    for (uint8_t i = 1; i < kept; i++) {
      msg += " " + utils::toHex(entry.data[i]);
    }
    if (entry.count > kept) {
      msg += " (+" + std::to_string(entry.count - kept) + " bytes)";
    }
    if (entry.nacked) {
      msg += " (NACK)";
    }
    debug::print_debug(msg);
  }

  if (writes.empty()) {
    if (const uint16_t dropped = writes.takeDropped(); dropped != 0) {
      debug::print_warn("ENCODER: " + std::to_string(dropped) + " writes not logged (log full)");
    }
  }
}

void init() {
  sniff_program.instructions = sniff_instructions;
  sniff_program.length = std::size(sniff_instructions);
  sniff_program.origin = -1;  // Anywhere, the jmp is relocated

  uint offset;
  if (!pio_claim_free_sm_and_add_program(&sniff_program, &pio, &sm, &offset)) {
    debug::print_warn("ENCODER: No free PIO state machine, sniffer off");
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
  sm_config_set_clkdiv(&c, (float)clock_get_hz(clk_sys) / 15000000.0f);
  pio_sm_init(pio, sm, offset + wrap_target, &c);

  dma_channel = dma_claim_unused_channel(false);
  if (dma_channel < 0) {
    debug::print_warn("ENCODER: No free DMA channel, sniffer off");
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
  debug::print_message("ENCODER: Watching the SMBus for the video encoder");
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
  printFound();
  EncoderMode::update();
  printWrites();
}

void resetFound() {
  found_mask = 0;
  logged_mask = 0;
  EncoderMode::reset();
}

void beginOwnTransfer() {
  if (dma_channel < 0) {
    return;
  }
  flush();
  decodeCaptured();  // Up to here it's the CPU's
  ignoring = true;
}

void endOwnTransfer() {
  if (dma_channel < 0) {
    return;
  }
  flush();
  decodeCaptured();  // Ours, not logged
  ignoring = false;
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
