#include "daedalus/protocol/schema.hpp"

#include <gtest/gtest.h>

using namespace daedalus::protocol;

TEST(SchemaParser, SingleModule) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "vehicle": {
                "signals": [
                    {"name": "position.x", "type": "f64", "unit": "m"},
                    {"name": "position.y", "type": "f64", "unit": "m"}
                ]
            }
        }
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.modules.size(), 1u);
    EXPECT_EQ(schema.modules[0].name, "vehicle");
    ASSERT_EQ(schema.modules[0].signals.size(), 2u);
    EXPECT_EQ(schema.modules[0].signals[0].name, "position.x");
    EXPECT_EQ(schema.modules[0].signals[0].type, "f64");
    ASSERT_TRUE(schema.modules[0].signals[0].unit.has_value());
    EXPECT_EQ(schema.modules[0].signals[0].unit.value(), "m");
}

TEST(SchemaParser, MultipleModules) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "rocket": {
                "signals": [
                    {"name": "position.x", "type": "f64", "unit": "m"},
                    {"name": "velocity.x", "type": "f64", "unit": "m/s"}
                ]
            },
            "inputs": {
                "signals": [
                    {"name": "throttle", "type": "f64"}
                ]
            }
        }
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.modules.size(), 2u);

    // Find each module (JSON object iteration order not guaranteed)
    const ModuleInfo *rocket = nullptr;
    const ModuleInfo *inputs = nullptr;
    for (auto &mod : schema.modules) {
        if (mod.name == "rocket")
            rocket = &mod;
        if (mod.name == "inputs")
            inputs = &mod;
    }

    ASSERT_NE(rocket, nullptr);
    EXPECT_EQ(rocket->signals.size(), 2u);

    ASSERT_NE(inputs, nullptr);
    ASSERT_EQ(inputs->signals.size(), 1u);
    EXPECT_EQ(inputs->signals[0].name, "throttle");
    EXPECT_FALSE(inputs->signals[0].unit.has_value());
}

TEST(SchemaParser, SignalWithoutUnit) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "ctrl": {
                "signals": [
                    {"name": "gain", "type": "f64"}
                ]
            }
        }
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.modules[0].signals.size(), 1u);
    EXPECT_FALSE(schema.modules[0].signals[0].unit.has_value());
}

TEST(SchemaParser, EmptyModules) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {}
    })");

    auto schema = parse_schema(msg);
    EXPECT_TRUE(schema.modules.empty());
}

TEST(SchemaParser, MissingType) {
    auto msg = nlohmann::json::parse(R"({
        "modules": {}
    })");

    EXPECT_THROW(parse_schema(msg), std::runtime_error);
}

TEST(SchemaParser, WrongType) {
    auto msg = nlohmann::json::parse(R"({
        "type": "ack",
        "modules": {}
    })");

    EXPECT_THROW(parse_schema(msg), std::runtime_error);
}

TEST(SchemaParser, MissingSignals) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "broken": {}
        }
    })");

    EXPECT_THROW(parse_schema(msg), std::runtime_error);
}

TEST(SubscribeAckParser, ValidAck) {
    auto msg = nlohmann::json::parse(R"({
        "type": "ack",
        "action": "subscribe",
        "count": 4,
        "signals": [
            "vehicle.position.x",
            "vehicle.position.y",
            "vehicle.velocity.x",
            "vehicle.velocity.y"
        ]
    })");

    auto ack = parse_subscribe_ack(msg);
    EXPECT_EQ(ack.count, 4u);
    ASSERT_EQ(ack.signals.size(), 4u);
    EXPECT_EQ(ack.signals[0], "vehicle.position.x");
    EXPECT_EQ(ack.signals[1], "vehicle.position.y");
    EXPECT_EQ(ack.signals[2], "vehicle.velocity.x");
    EXPECT_EQ(ack.signals[3], "vehicle.velocity.y");
}

TEST(SubscribeAckParser, OrderPreserved) {
    auto msg = nlohmann::json::parse(R"({
        "type": "ack",
        "action": "subscribe",
        "count": 3,
        "signals": ["z.signal", "a.signal", "m.signal"]
    })");

    auto ack = parse_subscribe_ack(msg);
    EXPECT_EQ(ack.signals[0], "z.signal");
    EXPECT_EQ(ack.signals[1], "a.signal");
    EXPECT_EQ(ack.signals[2], "m.signal");
}

TEST(SubscribeAckParser, WrongAction) {
    auto msg = nlohmann::json::parse(R"({
        "type": "ack",
        "action": "pause"
    })");

    EXPECT_THROW(parse_subscribe_ack(msg), std::runtime_error);
}
