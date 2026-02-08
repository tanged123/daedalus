#include "daedalus/data/telemetry_queue.hpp"

#include <gtest/gtest.h>

#include <thread>

using namespace daedalus::data;

TEST(SPSCQueue, PushPop) {
    SPSCQueue<int> q(4);
    int val = 0;

    EXPECT_TRUE(q.try_push(42));
    EXPECT_TRUE(q.try_pop(val));
    EXPECT_EQ(val, 42);
}

TEST(SPSCQueue, FIFO) {
    SPSCQueue<int> q(8);
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(q.try_push(std::move(i)));
    }

    int val = 0;
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(q.try_pop(val));
        EXPECT_EQ(val, i);
    }
}

TEST(SPSCQueue, PopFromEmpty) {
    SPSCQueue<int> q(4);
    int val = 0;
    EXPECT_FALSE(q.try_pop(val));
}

TEST(SPSCQueue, PushToFull) {
    SPSCQueue<int> q(4); // usable capacity = 3
    EXPECT_TRUE(q.try_push(1));
    EXPECT_TRUE(q.try_push(2));
    EXPECT_TRUE(q.try_push(3));
    EXPECT_FALSE(q.try_push(4)); // full
    EXPECT_EQ(q.capacity(), 3u);
}

TEST(SPSCQueue, WrapAround) {
    SPSCQueue<int> q(4); // capacity 3
    // Fill and drain several times to exercise wrap
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 3; ++i) {
            EXPECT_TRUE(q.try_push(round * 10 + i));
        }
        int val = 0;
        for (int i = 0; i < 3; ++i) {
            EXPECT_TRUE(q.try_pop(val));
            EXPECT_EQ(val, round * 10 + i);
        }
    }
}

TEST(SPSCQueue, SizeApprox) {
    SPSCQueue<int> q(8);
    EXPECT_EQ(q.size_approx(), 0u);
    q.try_push(1);
    q.try_push(2);
    EXPECT_EQ(q.size_approx(), 2u);
    int val = 0;
    q.try_pop(val);
    EXPECT_EQ(q.size_approx(), 1u);
}

TEST(TelemetryQueue, BinaryFrameTransfer) {
    TelemetryQueue q(16);
    std::vector<uint8_t> frame = {0x48, 0x45, 0x52, 0x54, 1, 2, 3, 4};

    EXPECT_TRUE(q.try_push(std::vector<uint8_t>(frame)));

    std::vector<uint8_t> out;
    EXPECT_TRUE(q.try_pop(out));
    EXPECT_EQ(out, frame);
}

TEST(EventQueue, JsonStringTransfer) {
    EventQueue q(16);
    std::string event = R"({"type":"schema","modules":{}})";

    EXPECT_TRUE(q.try_push(std::string(event)));

    std::string out;
    EXPECT_TRUE(q.try_pop(out));
    EXPECT_EQ(out, event);
}

TEST(SPSCQueue, MultithreadedStress) {
    constexpr int kCount = 10000;
    SPSCQueue<int> q(256);

    std::thread producer([&] {
        for (int i = 0; i < kCount; ++i) {
            while (!q.try_push(std::move(i))) {
                // spin
            }
        }
    });

    std::vector<int> received;
    received.reserve(kCount);

    std::thread consumer([&] {
        int val = 0;
        while (static_cast<int>(received.size()) < kCount) {
            if (q.try_pop(val)) {
                received.push_back(val);
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), static_cast<size_t>(kCount));
    for (int i = 0; i < kCount; ++i) {
        EXPECT_EQ(received[i], i) << "Mismatch at index " << i;
    }
}
