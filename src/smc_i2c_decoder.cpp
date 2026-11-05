#include "smc_i2c_decoder.hpp"
#include "smc_encoder_mode.hpp"
#include "smc.hpp"
#include "ring_buffer.hpp"

#include <iterator>
#include "text.hpp"

namespace SMC {
namespace I2CDecoder {

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
  if (!debug::enabled(debug::Level::Debug) || ignoring || decoder.restarted || decoder.byte_count < 3 || (decoder.bytes[0] & 1) != 0) {
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

void sample(const uint8_t state) {
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

static void printFound() {
  const uint8_t new_found = found_mask & ~logged_mask;
  for (uint8_t i = 0; i < std::size(encoders); i++) {
    if (new_found & (1u << i)) {
      debug::print(debug::Level::Info, "ENCODER: Found %s (0x%02X)", encoders[i].name, encoders[i].address);
    }
  }
  logged_mask |= new_found;
}

static void printWrites() {
  LogEntry entry;
  for (uint8_t n = 0; n < prints_per_tick && writes.pop(entry); n++) {
    Text<64> msg;
    msg.append("ENCODER: %s [0x%02X] =", encoderName(entry.address), entry.data[0]);
    const uint8_t kept = entry.count < max_bytes - 1 ? entry.count : max_bytes - 1;
    for (uint8_t i = 1; i < kept; i++) {
      msg.append(" 0x%02X", entry.data[i]);
    }
    if (entry.count > kept) {
      msg.append(" (+%u bytes)", entry.count - kept);
    }
    if (entry.nacked) {
      msg += " (NACK)";
    }
    debug::print(debug::Level::Debug, "%s", msg.c_str());
  }

  if (writes.empty()) {
    if (const uint16_t dropped = writes.takeDropped(); dropped != 0) {
      debug::print(debug::Level::Warnings, "ENCODER: %u writes not logged (log full)", dropped);
    }
  }
}

void report() {
  printFound();
  EncoderMode::update();
  printWrites();
}

void resetFound() {
  found_mask = 0;
  logged_mask = 0;
  EncoderMode::reset();
}

void setIgnoring(const bool ignore) {
  ignoring = ignore;
}

}  // namespace I2CDecoder
}  // namespace SMC
