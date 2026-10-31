#include "utils.hpp"

#include <cstdio>

namespace utils {

void setBitNo(uint8_t& bits, const uint8_t bit_no) {
  bits |= (1 << bit_no);
}

void clearBitNo(uint8_t& bits, const uint8_t bit_no) {
  bits &= ~(1 << bit_no);
}

uint8_t checkBitNo(const uint8_t bits, const uint8_t bit_no) {
  return ((bits) & (1 << (bit_no)));
}

std::string toHex(const uint8_t value) {
  char text[5];
  snprintf(text, sizeof(text), "0x%02X", value);
  return text;
}

} // namespace utils
