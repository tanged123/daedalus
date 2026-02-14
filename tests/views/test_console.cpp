#include "daedalus/views/console.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

using namespace daedalus::views;

TEST(ConsoleLog, AddEntryIncreasesSize) {
    ConsoleLog log(8);
    EXPECT_TRUE(log.empty());

    log.add(ConsoleEntryType::System, "Connected");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().type, ConsoleEntryType::System);
    EXPECT_EQ(log.entries().back().message, "Connected");
}

TEST(ConsoleLog, CapacityIsEnforcedWithRollingBuffer) {
    ConsoleLog log(3);
    log.add(ConsoleEntryType::System, "one");
    log.add(ConsoleEntryType::System, "two");
    log.add(ConsoleEntryType::System, "three");
    log.add(ConsoleEntryType::System, "four");

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log.entries().front().message, "two");
    EXPECT_EQ(log.entries().back().message, "four");
}

TEST(ConsoleLog, AddFromJsonParsesEventMessage) {
    ConsoleLog log;
    log.add_from_json(R"({"type":"event","event":"paused"})");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().type, ConsoleEntryType::Event);
    EXPECT_EQ(log.entries().back().message, "paused");
}

TEST(ConsoleLog, AddFromJsonParsesAckMessage) {
    ConsoleLog log;
    log.add_from_json(R"({"type":"ack","action":"subscribe","count":4})");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().type, ConsoleEntryType::Ack);
    EXPECT_EQ(log.entries().back().message, "subscribe (4 signals)");
}

TEST(ConsoleLog, AddFromJsonParsesStepAckFormatting) {
    ConsoleLog log;
    log.add_from_json(R"({"type":"ack","action":"step","count":10,"frame":110})");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().message, "step x10 -> frame 110");
}

TEST(ConsoleLog, AddFromJsonParsesSetAckFormatting) {
    ConsoleLog log;
    log.add_from_json(R"({"type":"ack","action":"set","signal":"inputs.throttle","value":1.0})");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().message, "set inputs.throttle = 1");
}

TEST(ConsoleLog, AddFromJsonParsesErrorMessage) {
    ConsoleLog log;
    log.add_from_json(R"({"type":"error","message":"Unknown signal"})");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().type, ConsoleEntryType::Error);
    EXPECT_EQ(log.entries().back().message, "Unknown signal");
}

TEST(ConsoleLog, AddCommandCreatesCommandEntryWithJsonDetail) {
    ConsoleLog log;
    log.add_command("step", {{"count", 5}});

    ASSERT_EQ(log.size(), 1u);
    const auto &entry = log.entries().back();
    EXPECT_EQ(entry.type, ConsoleEntryType::Command);
    EXPECT_EQ(entry.message, "step x5");

    const auto parsed = nlohmann::json::parse(entry.detail);
    EXPECT_EQ(parsed.value("action", ""), "step");
    ASSERT_TRUE(parsed.contains("params"));
    EXPECT_EQ(parsed["params"].value("count", 0), 5);
}

TEST(ConsoleLog, ClearRemovesAllEntries) {
    ConsoleLog log;
    log.add(ConsoleEntryType::System, "Connected");
    log.add(ConsoleEntryType::Event, "running");
    EXPECT_EQ(log.size(), 2u);

    log.clear();
    EXPECT_TRUE(log.empty());
}

TEST(ConsoleLog, ElapsedIsNonNegativeAndIncreases) {
    ConsoleLog log;
    const double t0 = log.elapsed();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const double t1 = log.elapsed();
    EXPECT_GE(t0, 0.0);
    EXPECT_GT(t1, t0);
}

TEST(ConsoleLog, AddFromJsonHandlesMalformedJsonAsSystemMessage) {
    ConsoleLog log;
    log.add_from_json("{");

    ASSERT_EQ(log.size(), 1u);
    EXPECT_EQ(log.entries().back().type, ConsoleEntryType::System);
    EXPECT_EQ(log.entries().back().message, "Malformed JSON message");
}
