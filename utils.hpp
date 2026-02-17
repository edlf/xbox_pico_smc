#ifndef __UTILS__
#define __UTILS__

#include <stdint.h>

namespace utils {

void setBitNo(uint8_t& bits, const uint8_t bit_no);
void clearBitNo(uint8_t& bits, const uint8_t bit_no);
uint8_t checkBitNo(const uint8_t bits, const uint8_t bit_no);

} // namespace utils

#endif // __UTILS__
