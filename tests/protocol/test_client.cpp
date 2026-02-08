#include "daedalus/protocol/client.hpp"

#include <gtest/gtest.h>

using namespace daedalus::protocol;

TEST(HermesClient, FormatSubscribe) {
    auto cmd = HermesClient::format_command("subscribe", {{"signals", {"*"}}});

    EXPECT_EQ(cmd["action"], "subscribe");
    EXPECT_TRUE(cmd.contains("params"));
    EXPECT_EQ(cmd["params"]["signals"][0], "*");
}

TEST(HermesClient, FormatSubscribeMultiple) {
    auto cmd = HermesClient::format_command(
        "subscribe", {{"signals", {"vehicle.position.*", "vehicle.velocity.*"}}});

    EXPECT_EQ(cmd["action"], "subscribe");
    auto &sigs = cmd["params"]["signals"];
    ASSERT_EQ(sigs.size(), 2u);
    EXPECT_EQ(sigs[0], "vehicle.position.*");
    EXPECT_EQ(sigs[1], "vehicle.velocity.*");
}

TEST(HermesClient, FormatPause) {
    auto cmd = HermesClient::format_command("pause");

    EXPECT_EQ(cmd["action"], "pause");
    EXPECT_FALSE(cmd.contains("params"));
}

TEST(HermesClient, FormatResume) {
    auto cmd = HermesClient::format_command("resume");
    EXPECT_EQ(cmd["action"], "resume");
    EXPECT_FALSE(cmd.contains("params"));
}

TEST(HermesClient, FormatReset) {
    auto cmd = HermesClient::format_command("reset");
    EXPECT_EQ(cmd["action"], "reset");
}

TEST(HermesClient, FormatStep) {
    auto cmd = HermesClient::format_command("step", {{"count", 10}});

    EXPECT_EQ(cmd["action"], "step");
    EXPECT_EQ(cmd["params"]["count"], 10);
}

TEST(HermesClient, FormatSet) {
    auto cmd =
        HermesClient::format_command("set", {{"signal", "inputs.throttle"}, {"value", 0.75}});

    EXPECT_EQ(cmd["action"], "set");
    EXPECT_EQ(cmd["params"]["signal"], "inputs.throttle");
    EXPECT_DOUBLE_EQ(cmd["params"]["value"].get<double>(), 0.75);
}

TEST(HermesClient, NoTypeWrapper) {
    // Hermes protocol uses {"action": "..."} — NO {"type": "cmd"} wrapper
    auto cmd = HermesClient::format_command("pause");
    EXPECT_FALSE(cmd.contains("type"));
    EXPECT_TRUE(cmd.contains("action"));
}
