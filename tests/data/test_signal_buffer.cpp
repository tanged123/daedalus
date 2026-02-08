#include "daedalus/data/signal_buffer.hpp"

#include <gtest/gtest.h>

using namespace daedalus::data;

TEST(SignalBuffer, InitialState) {
    SignalBuffer buf(100);
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(buf.capacity(), 100u);
    EXPECT_FALSE(buf.full());
    EXPECT_TRUE(buf.empty());
}

TEST(SignalBuffer, PushIncrementsSize) {
    SignalBuffer buf(100);
    buf.push(0.0, 1.0);
    EXPECT_EQ(buf.size(), 1u);
    buf.push(0.1, 2.0);
    EXPECT_EQ(buf.size(), 2u);
}

TEST(SignalBuffer, AccessValues) {
    SignalBuffer buf(100);
    buf.push(0.0, 10.0);
    buf.push(0.1, 20.0);
    buf.push(0.2, 30.0);

    // Index 0 = oldest
    EXPECT_DOUBLE_EQ(buf.time_at(0), 0.0);
    EXPECT_DOUBLE_EQ(buf.value_at(0), 10.0);
    EXPECT_DOUBLE_EQ(buf.value_at(2), 30.0);
}

TEST(SignalBuffer, LastValue) {
    SignalBuffer buf(100);
    buf.push(0.0, 42.0);
    EXPECT_DOUBLE_EQ(buf.last_value(), 42.0);
    EXPECT_DOUBLE_EQ(buf.last_time(), 0.0);

    buf.push(1.0, 99.0);
    EXPECT_DOUBLE_EQ(buf.last_value(), 99.0);
    EXPECT_DOUBLE_EQ(buf.last_time(), 1.0);
}

TEST(SignalBuffer, WrapAround) {
    SignalBuffer buf(4);
    buf.push(0.0, 1.0);
    buf.push(1.0, 2.0);
    buf.push(2.0, 3.0);
    buf.push(3.0, 4.0);
    EXPECT_TRUE(buf.full());
    EXPECT_EQ(buf.size(), 4u);

    // Push one more — oldest (1.0) should be overwritten
    buf.push(4.0, 5.0);
    EXPECT_EQ(buf.size(), 4u);
    EXPECT_TRUE(buf.full());

    // Oldest is now value 2.0
    EXPECT_DOUBLE_EQ(buf.value_at(0), 2.0);
    EXPECT_DOUBLE_EQ(buf.time_at(0), 1.0);
    // Newest is value 5.0
    EXPECT_DOUBLE_EQ(buf.last_value(), 5.0);
    EXPECT_DOUBLE_EQ(buf.last_time(), 4.0);
}

TEST(SignalBuffer, ExactCapacity) {
    SignalBuffer buf(3);
    buf.push(0.0, 10.0);
    buf.push(1.0, 20.0);
    buf.push(2.0, 30.0);

    EXPECT_TRUE(buf.full());
    EXPECT_DOUBLE_EQ(buf.value_at(0), 10.0);
    EXPECT_DOUBLE_EQ(buf.value_at(1), 20.0);
    EXPECT_DOUBLE_EQ(buf.value_at(2), 30.0);
}

TEST(SignalBuffer, Clear) {
    SignalBuffer buf(100);
    buf.push(0.0, 1.0);
    buf.push(1.0, 2.0);
    buf.clear();
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_TRUE(buf.empty());
    EXPECT_FALSE(buf.full());
}

TEST(SignalBuffer, CopyTo) {
    SignalBuffer buf(4);
    buf.push(0.0, 10.0);
    buf.push(1.0, 20.0);
    buf.push(2.0, 30.0);
    buf.push(3.0, 40.0);
    buf.push(4.0, 50.0); // wraps, oldest=20.0

    std::vector<double> times, values;
    buf.copy_to(times, values);

    ASSERT_EQ(times.size(), 4u);
    ASSERT_EQ(values.size(), 4u);

    // Oldest first
    EXPECT_DOUBLE_EQ(times[0], 1.0);
    EXPECT_DOUBLE_EQ(values[0], 20.0);
    EXPECT_DOUBLE_EQ(times[3], 4.0);
    EXPECT_DOUBLE_EQ(values[3], 50.0);
}
