// RingBuffer (src/ring_buffer.hpp), the SMBus and sniffer logs

#include <gtest/gtest.h>

#include "ring_buffer.hpp"

namespace {

TEST(RingBufferTest, EmptyAtStart)
{
  RingBuffer<int, 4> buffer;
  int                item = 0;
  EXPECT_TRUE(buffer.empty());
  EXPECT_FALSE(buffer.pop(item));
  EXPECT_EQ(buffer.takeDropped(), 0);
}

TEST(RingBufferTest, FirstInFirstOut)
{
  RingBuffer<int, 4> buffer;
  EXPECT_TRUE(buffer.push(1));
  EXPECT_TRUE(buffer.push(2));
  int item = 0;
  EXPECT_TRUE(buffer.pop(item));
  EXPECT_EQ(item, 1);
  EXPECT_TRUE(buffer.pop(item));
  EXPECT_EQ(item, 2);
  EXPECT_TRUE(buffer.empty());
}

TEST(RingBufferTest, HoldsOneLessThanItsSizeAndCountsDrops)
{
  RingBuffer<int, 4> buffer;
  EXPECT_TRUE(buffer.push(1));
  EXPECT_TRUE(buffer.push(2));
  EXPECT_TRUE(buffer.push(3));
  EXPECT_FALSE(buffer.push(4));
  EXPECT_FALSE(buffer.push(5));
  EXPECT_EQ(buffer.takeDropped(), 2);
  EXPECT_EQ(buffer.takeDropped(), 0);  // Counted since the last call

  int item = 0;
  EXPECT_TRUE(buffer.pop(item));
  EXPECT_EQ(item, 1);  // The oldest kept, the newest dropped
}

TEST(RingBufferTest, WrapsAround)
{
  RingBuffer<int, 4> buffer;
  int                item = 0;
  for (int i = 0; i < 20; i++) {
    EXPECT_TRUE(buffer.push(i));
    EXPECT_TRUE(buffer.push(i + 100));
    EXPECT_TRUE(buffer.pop(item));
    EXPECT_EQ(item, i);
    EXPECT_TRUE(buffer.pop(item));
    EXPECT_EQ(item, i + 100);
  }
  EXPECT_TRUE(buffer.empty());
  EXPECT_EQ(buffer.takeDropped(), 0);
}

}  // namespace
