#include "utils.hpp"

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

} // namespace utils
