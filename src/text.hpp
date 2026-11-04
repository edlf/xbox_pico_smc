#ifndef TEXT_HPP
#define TEXT_HPP

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

// Fixed-size text for building a message in pieces without the heap. What
// doesn't fit is cut off, the text always stays zero-terminated.
template <size_t N>
class Text {
  static_assert(N >= 2, "Room for a character and the terminator");

 public:
  Text() {
    text[0] = '\0';
  }

  explicit Text(const char* s) : Text() {
    *this += s;
  }

  Text& operator+=(const char* s) {
    return append("%s", s);
  }

  // printf-style
  __attribute__((format(printf, 2, 3))) Text& append(const char* format, ...) {
    if (used < N - 1) {
      va_list args;
      va_start(args, format);
      const int written = vsnprintf(text + used, N - used, format, args);
      va_end(args);
      if (written > 0) {
        const size_t room = N - 1 - used;
        used += static_cast<size_t>(written) < room ? static_cast<size_t>(written) : room;
      }
    }
    return *this;
  }

  const char* c_str() const {
    return text;
  }
  size_t length() const {
    return used;
  }
  bool empty() const {
    return used == 0;
  }

  void clear() {
    used = 0;
    text[0] = '\0';
  }

  bool operator==(const char* s) const {
    return strcmp(text, s) == 0;
  }
  bool operator!=(const char* s) const {
    return !(*this == s);
  }

  template <size_t M>
  bool operator==(const Text<M>& other) const {
    return *this == other.c_str();
  }
  template <size_t M>
  bool operator!=(const Text<M>& other) const {
    return !(*this == other.c_str());
  }

 private:
  char   text[N];
  size_t used = 0;
};

#endif  // TEXT_HPP
