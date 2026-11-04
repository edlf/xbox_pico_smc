#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <atomic>
#include <cstdint>

// Fixed-size FIFO with one producer and one consumer, which can be an
// interrupt and the main loop: push() only moves the head, pop() only the
// tail. When it's full push() drops the item and counts it. Holds N - 1 items.
template <typename T, uint8_t N>
class RingBuffer {
 public:
  bool push(const T& item) {
    const uint8_t next = static_cast<uint8_t>((head + 1) % N);
    if (next == tail) {
      dropped = static_cast<uint16_t>(dropped + 1);
      return false;
    }
    items[head] = item;
    std::atomic_signal_fence(std::memory_order_release);  // The item before the head
    head = next;
    return true;
  }

  bool pop(T& item) {
    if (tail == head) {
      return false;
    }
    std::atomic_signal_fence(std::memory_order_acquire);
    item = items[tail];
    tail = static_cast<uint8_t>((tail + 1) % N);
    return true;
  }

  bool empty() const {
    return tail == head;
  }

  // Drops what's waiting. Consumer side, like pop().
  void clear() {
    tail = head;
  }

  // Items dropped since the last call
  uint16_t takeDropped() {
    const uint16_t count = dropped;
    dropped = 0;
    return count;
  }

 private:
  T                 items[N] = {};
  volatile uint8_t  head = 0;  // Written by the producer
  volatile uint8_t  tail = 0;  // Written by the consumer
  volatile uint16_t dropped = 0;
};

#endif  // RING_BUFFER_HPP
