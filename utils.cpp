#include "utils.hpp"

#include <cstdio>

namespace utils {

std::string toHex(const uint8_t value) {
  char text[5];
  snprintf(text, sizeof(text), "0x%02X", value);
  return text;
}

} // namespace utils
