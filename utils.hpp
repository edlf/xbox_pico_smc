#ifndef __UTILS__
#define __UTILS__

#include <cstdint>
#include <string>

namespace utils {

void setBitNo(uint8_t& bits, const uint8_t bit_no);
void clearBitNo(uint8_t& bits, const uint8_t bit_no);
uint8_t checkBitNo(const uint8_t bits, const uint8_t bit_no);

// "0x0C", always two digits
std::string toHex(const uint8_t value);

} // namespace utils

#endif // __UTILS__
