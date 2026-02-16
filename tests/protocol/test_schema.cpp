#include "daedalus/protocol/schema.hpp"

#include <gtest/gtest.h>

#include <algorithm>

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
    EXPECT_TRUE(schema.wiring.empty());
}

TEST(SchemaParser, ParsesWiringEntries) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {
                "signals": [{"name": "thrust_cmd", "type": "f64", "unit": "N"}]
            },
            "physics": {
                "signals": [{"name": "input", "type": "f64"}]
            }
        },
        "wiring": [
            {
                "src": "inputs.thrust_cmd",
                "dst": "physics.input",
                "gain": 2.0,
                "offset": -1.25
            }
        ]
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.wiring.size(), 1u);
    EXPECT_EQ(schema.wiring[0].src, "inputs.thrust_cmd");
    EXPECT_EQ(schema.wiring[0].dst, "physics.input");
    EXPECT_DOUBLE_EQ(schema.wiring[0].gain, 2.0);
    EXPECT_DOUBLE_EQ(schema.wiring[0].offset, -1.25);
}

TEST(SchemaParser, MissingWiringDefaultsToEmpty) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {
                "signals": [{"name": "thrust_cmd", "type": "f64"}]
            }
        }
    })");

    auto schema = parse_schema(msg);
    EXPECT_TRUE(schema.wiring.empty());
}

TEST(SchemaParser, ParsesModuleIntrospectionHints) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "rocket": {
                "signals": [{"name": "state", "type": "f64"}],
                "module_type": "icarus",
                "supports_introspection": true,
                "component_count": 5
            }
        }
    })");

    const auto schema = parse_schema(msg);
    ASSERT_EQ(schema.modules.size(), 1u);
    ASSERT_TRUE(schema.modules[0].module_type.has_value());
    ASSERT_TRUE(schema.modules[0].supports_introspection.has_value());
    ASSERT_TRUE(schema.modules[0].component_count.has_value());
    EXPECT_EQ(schema.modules[0].module_type.value(), "icarus");
    EXPECT_TRUE(schema.modules[0].supports_introspection.value());
    EXPECT_EQ(schema.modules[0].component_count.value(), 5u);
}

TEST(SchemaParser, MissingIntrospectionHintsRemainUnset) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {
                "signals": [{"name": "throttle", "type": "f64"}]
            }
        }
    })");

    const auto schema = parse_schema(msg);
    ASSERT_EQ(schema.modules.size(), 1u);
    EXPECT_FALSE(schema.modules[0].module_type.has_value());
    EXPECT_FALSE(schema.modules[0].supports_introspection.has_value());
    EXPECT_FALSE(schema.modules[0].component_count.has_value());
}

TEST(SchemaParser, EmptyWiringArray) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {
                "signals": [{"name": "thrust_cmd", "type": "f64"}]
            }
        },
        "wiring": []
    })");

    auto schema = parse_schema(msg);
    EXPECT_TRUE(schema.wiring.empty());
}

TEST(SchemaParser, WiringGainAndOffsetDefaultValues) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {"signals": [{"name": "thrust_cmd", "type": "f64"}]},
            "physics": {"signals": [{"name": "input", "type": "f64"}]}
        },
        "wiring": [
            {"src": "inputs.thrust_cmd", "dst": "physics.input"}
        ]
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.wiring.size(), 1u);
    EXPECT_DOUBLE_EQ(schema.wiring[0].gain, 1.0);
    EXPECT_DOUBLE_EQ(schema.wiring[0].offset, 0.0);
}

TEST(SchemaParser, MalformedWireMissingSourceIsSkipped) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {"signals": [{"name": "thrust_cmd", "type": "f64"}]},
            "physics": {"signals": [{"name": "input", "type": "f64"}]}
        },
        "wiring": [
            {"dst": "physics.input"},
            {"src": "inputs.thrust_cmd", "dst": "physics.input"}
        ]
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.wiring.size(), 1u);
    EXPECT_EQ(schema.wiring[0].src, "inputs.thrust_cmd");
    EXPECT_EQ(schema.wiring[0].dst, "physics.input");
}

TEST(SchemaParser, MalformedWireMissingDestinationIsSkipped) {
    auto msg = nlohmann::json::parse(R"({
        "type": "schema",
        "modules": {
            "inputs": {"signals": [{"name": "thrust_cmd", "type": "f64"}]},
            "physics": {"signals": [{"name": "input", "type": "f64"}]}
        },
        "wiring": [
            {"src": "inputs.thrust_cmd"},
            {"src": "inputs.thrust_cmd", "dst": "physics.input"}
        ]
    })");

    auto schema = parse_schema(msg);
    ASSERT_EQ(schema.wiring.size(), 1u);
    EXPECT_EQ(schema.wiring[0].src, "inputs.thrust_cmd");
    EXPECT_EQ(schema.wiring[0].dst, "physics.input");
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

TEST(IntrospectAckParser, ParsesValidAck) {
    auto msg = nlohmann::json::parse(R"({
        "type": "ack",
        "action": "introspect",
        "module": "rocket",
        "module_type": "icarus",
        "components": [
            {
                "name": "Rocket.Engine",
                "type": "SolidRocketEngine",
                "outputs": [
                    {"name": "Rocket.Engine.thrust", "unit": "N"}
                ],
                "inputs": [
                    {"name": "Rocket.Engine.throttle_cmd", "unit": ""}
                ],
                "parameters": [
                    {"name": "Rocket.Engine.max_thrust", "unit": "N"}
                ]
            }
        ],
        "internal_wiring": [
            {"src": "Rocket.Engine.thrust", "dst": "Rocket.Body.force.x"}
        ],
        "execution_order": ["Rocket.Engine", "Rocket.Body"],
        "summary": {"total_components": 2}
    })");

    const auto ack = parse_introspect_ack(msg);
    EXPECT_EQ(ack.module, "rocket");
    ASSERT_TRUE(ack.module_type.has_value());
    EXPECT_EQ(ack.module_type.value(), "icarus");
    ASSERT_EQ(ack.components.size(), 1u);
    EXPECT_EQ(ack.components[0].name, "Rocket.Engine");
    EXPECT_EQ(ack.components[0].type, "SolidRocketEngine");
    ASSERT_EQ(ack.components[0].outputs.size(), 1u);
    EXPECT_EQ(ack.components[0].outputs[0].name, "Rocket.Engine.thrust");
    ASSERT_TRUE(ack.components[0].outputs[0].unit.has_value());
    EXPECT_EQ(ack.components[0].outputs[0].unit.value(), "N");
    ASSERT_EQ(ack.internal_wiring.size(), 1u);
    EXPECT_EQ(ack.internal_wiring[0].src, "Rocket.Engine.thrust");
    EXPECT_EQ(ack.internal_wiring[0].dst, "Rocket.Body.force.x");
    ASSERT_EQ(ack.execution_order.size(), 2u);
    EXPECT_EQ(ack.execution_order[0], "Rocket.Engine");
    EXPECT_EQ(ack.execution_order[1], "Rocket.Body");
    ASSERT_TRUE(ack.summary.contains("total_components"));
}

TEST(IntrospectAckParser, MissingModuleThrows) {
    auto msg = nlohmann::json::parse(R"({
        "type": "ack",
        "action": "introspect",
        "components": []
    })");

    EXPECT_THROW(parse_introspect_ack(msg), std::runtime_error);
}

TEST(IntrospectionSchemaBuilder, ConvertsComponentsAndWiringToSchema) {
    IntrospectAck ack;
    ack.module = "rocket";
    ack.module_type = "icarus";
    ack.components = {
        IntrospectionComponentInfo{
            .name = "Rocket.Engine",
            .type = "SolidRocketEngine",
            .inputs = {IntrospectionSignalInfo{"Rocket.Engine.throttle_cmd", std::nullopt}},
            .outputs = {IntrospectionSignalInfo{"Rocket.Engine.thrust", std::string("N")}},
        },
        IntrospectionComponentInfo{
            .name = "Rocket.Body",
            .type = "Vehicle6DOF",
            .inputs = {IntrospectionSignalInfo{"Rocket.Body.force.x", std::string("N")}},
            .outputs = {IntrospectionSignalInfo{"Rocket.Body.velocity.x", std::string("m/s")}},
        },
    };
    ack.internal_wiring = {
        WireInfo{"Rocket.Engine.thrust", "Rocket.Body.force.x", 1.0, 0.0},
    };

    const auto schema = make_schema_from_introspection(ack);
    ASSERT_EQ(schema.modules.size(), 2u);
    ASSERT_EQ(schema.wiring.size(), 1u);
    EXPECT_EQ(schema.wiring[0].src, "Rocket.Engine.thrust");
    EXPECT_EQ(schema.wiring[0].dst, "Rocket.Body.force.x");

    const auto engine =
        std::find_if(schema.modules.begin(), schema.modules.end(),
                     [](const ModuleInfo &module) { return module.name == "Rocket.Engine"; });
    ASSERT_NE(engine, schema.modules.end());
    EXPECT_TRUE(engine->supports_introspection.has_value());
    EXPECT_TRUE(engine->supports_introspection.value());
    EXPECT_EQ(engine->signals[0].name, "throttle_cmd");
    EXPECT_EQ(engine->signals[1].name, "thrust");
}

TEST(IntrospectionSchemaBuilder, AddsSignalsReferencedOnlyByWiring) {
    IntrospectAck ack;
    ack.module = "rocket";
    ack.components = {
        IntrospectionComponentInfo{
            .name = "Rocket.Engine",
            .type = "SolidRocketEngine",
        },
        IntrospectionComponentInfo{
            .name = "Rocket.Body",
            .type = "Vehicle6DOF",
        },
    };
    ack.internal_wiring = {
        WireInfo{"Rocket.Engine.thrust", "Rocket.Body.force.x", 1.0, 0.0},
    };

    const auto schema = make_schema_from_introspection(ack);
    ASSERT_EQ(schema.modules.size(), 2u);

    const auto engine =
        std::find_if(schema.modules.begin(), schema.modules.end(),
                     [](const ModuleInfo &module) { return module.name == "Rocket.Engine"; });
    const auto body =
        std::find_if(schema.modules.begin(), schema.modules.end(),
                     [](const ModuleInfo &module) { return module.name == "Rocket.Body"; });
    ASSERT_NE(engine, schema.modules.end());
    ASSERT_NE(body, schema.modules.end());
    ASSERT_EQ(engine->signals.size(), 1u);
    ASSERT_EQ(body->signals.size(), 1u);
    EXPECT_EQ(engine->signals[0].name, "thrust");
    EXPECT_EQ(body->signals[0].name, "force.x");
}
