#include "daedalus/views/topology.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <string>

using daedalus::protocol::ModuleInfo;
using daedalus::protocol::Schema;
using daedalus::protocol::SignalInfo;
using daedalus::protocol::SubscribeAck;
using daedalus::protocol::WireInfo;
using daedalus::views::TopologyGraph;
using daedalus::views::TopologyNode;

namespace {

ModuleInfo make_module(const std::string &name, std::initializer_list<const char *> signals) {
    ModuleInfo module;
    module.name = name;
    for (const char *signal_name : signals) {
        module.signals.push_back(SignalInfo{signal_name, "f64", std::nullopt});
    }
    return module;
}

const TopologyNode *find_node(const TopologyGraph &graph, const std::string &module_name) {
    for (const auto &node : graph.nodes()) {
        if (node.module_name == module_name) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace

TEST(TopologyGraph, BuildFromSchemaWithWiringCreatesNodesPinsAndLinks) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd", "pitch_cmd"}));
    schema.modules.push_back(make_module("physics", {"input", "output"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    ASSERT_EQ(graph.nodes().size(), 2u);
    ASSERT_EQ(graph.links().size(), 1u);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    EXPECT_EQ(inputs->output_pins.size(), 1u);
    EXPECT_EQ(inputs->input_pins.size(), 0u);
    EXPECT_EQ(physics->input_pins.size(), 1u);
    EXPECT_EQ(physics->output_pins.size(), 0u);
}

TEST(TopologyGraph, BuildFromSchemaWithoutWiringCreatesStandaloneNodes) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));

    TopologyGraph graph;
    graph.build_from_schema(schema);

    ASSERT_EQ(graph.nodes().size(), 2u);
    EXPECT_TRUE(graph.links().empty());
    EXPECT_FALSE(graph.has_wiring());

    for (const auto &node : graph.nodes()) {
        EXPECT_TRUE(node.input_pins.empty());
        EXPECT_TRUE(node.output_pins.empty());
    }
}

TEST(TopologyGraph, EmptySchemaProducesEmptyGraph) {
    TopologyGraph graph;
    Schema schema;
    graph.build_from_schema(schema);
    EXPECT_TRUE(graph.empty());
    EXPECT_TRUE(graph.nodes().empty());
    EXPECT_TRUE(graph.links().empty());
}

TEST(TopologyGraph, PinDirectionMatchesWireEndpoints) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    ASSERT_EQ(inputs->output_pins.size(), 1u);
    ASSERT_EQ(physics->input_pins.size(), 1u);
    EXPECT_EQ(inputs->output_pins[0].signal_path, "inputs.thrust_cmd");
    EXPECT_EQ(physics->input_pins[0].signal_path, "physics.input");
}

TEST(TopologyGraph, LinkReferencesValidPinIds) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    ASSERT_EQ(graph.links().size(), 1u);
    const auto &link = graph.links()[0];
    EXPECT_EQ(link.id, TopologyGraph::kLinkIdBase);
    EXPECT_EQ(link.source_pin_id, TopologyGraph::kPinIdBase);
    EXPECT_EQ(link.dest_pin_id, TopologyGraph::kPinIdBase + TopologyGraph::kMaxPinsPerModule);
}

TEST(TopologyGraph, LinkPreservesGainAndOffset) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 2.5, -0.75});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    ASSERT_EQ(graph.links().size(), 1u);
    EXPECT_DOUBLE_EQ(graph.links()[0].gain, 2.5);
    EXPECT_DOUBLE_EQ(graph.links()[0].offset, -0.75);
}

TEST(TopologyGraph, UnwiredSignalCountTracksSignalsNotInAnyWire) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd", "pitch_cmd"}));
    schema.modules.push_back(make_module("physics", {"input", "output", "state"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    EXPECT_EQ(inputs->unwired_signal_count, 1u);  // pitch_cmd
    EXPECT_EQ(physics->unwired_signal_count, 2u); // output, state
}

TEST(TopologyGraph, TotalSignalCountReflectsSchemaSize) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd", "pitch_cmd"}));
    schema.modules.push_back(make_module("physics", {"input", "output", "state"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    EXPECT_EQ(inputs->total_signal_count, 2u);
    EXPECT_EQ(physics->total_signal_count, 3u);
}

TEST(TopologyGraph, DuplicateSignalAcrossMultipleWiresCreatesSinglePin) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input_a", "input_b"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input_a", 1.0, 0.0});
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input_b", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *inputs = find_node(graph, "inputs");
    ASSERT_NE(inputs, nullptr);
    EXPECT_EQ(inputs->output_pins.size(), 1u);
    ASSERT_EQ(graph.links().size(), 2u);
    EXPECT_EQ(graph.links()[0].source_pin_id, graph.links()[1].source_pin_id);
}

TEST(TopologyGraph, HasWiringReflectsLinkPresence) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));

    TopologyGraph graph;
    graph.build_from_schema(schema);
    EXPECT_FALSE(graph.has_wiring());

    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});
    graph.build_from_schema(schema);
    EXPECT_TRUE(graph.has_wiring());
}

TEST(TopologyGraph, ClearRemovesAllGraphData) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);
    ASSERT_FALSE(graph.empty());
    ASSERT_FALSE(graph.links().empty());

    graph.clear();
    EXPECT_TRUE(graph.empty());
    EXPECT_TRUE(graph.nodes().empty());
    EXPECT_TRUE(graph.links().empty());
}

TEST(TopologyGraph, UpdateSubscriptionAssignsSignalIndicesToMatchingPins) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    SubscribeAck ack;
    ack.count = 2;
    ack.signals = {"physics.input", "inputs.thrust_cmd"};
    graph.update_subscription(ack);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    ASSERT_EQ(inputs->output_pins.size(), 1u);
    ASSERT_EQ(physics->input_pins.size(), 1u);
    ASSERT_TRUE(inputs->output_pins[0].signal_index.has_value());
    ASSERT_TRUE(physics->input_pins[0].signal_index.has_value());
    EXPECT_EQ(inputs->output_pins[0].signal_index.value(), 1u);
    EXPECT_EQ(physics->input_pins[0].signal_index.value(), 0u);
}

TEST(TopologyGraph, UpdateUnitsAssignsUnitBySignalPath) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);
    graph.update_units({
        {"inputs.thrust_cmd", "N"},
        {"physics.input", "N"},
    });

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    ASSERT_EQ(inputs->output_pins.size(), 1u);
    ASSERT_EQ(physics->input_pins.size(), 1u);
    ASSERT_TRUE(inputs->output_pins[0].unit.has_value());
    ASSERT_TRUE(physics->input_pins[0].unit.has_value());
    EXPECT_EQ(inputs->output_pins[0].unit.value(), "N");
    EXPECT_EQ(physics->input_pins[0].unit.value(), "N");
}

TEST(TopologyGraphLayout, SingleModuleAtOrigin) {
    Schema schema;
    schema.modules.push_back(make_module("solo", {"a"}));

    TopologyGraph graph;
    graph.build_from_schema(schema);

    ASSERT_EQ(graph.nodes().size(), 1u);
    EXPECT_FLOAT_EQ(graph.nodes()[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(graph.nodes()[0].position.y, 0.0f);
}

TEST(TopologyGraphLayout, TwoModulesOneWirePlacesConsumerToRight) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd"}));
    schema.modules.push_back(make_module("physics", {"input"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    EXPECT_FLOAT_EQ(inputs->position.x, 0.0f);
    EXPECT_FLOAT_EQ(physics->position.x, 300.0f);
}

TEST(TopologyGraphLayout, ChainPlacesNodesInIncreasingLayers) {
    Schema schema;
    schema.modules.push_back(make_module("a", {"x"}));
    schema.modules.push_back(make_module("b", {"x"}));
    schema.modules.push_back(make_module("c", {"x"}));
    schema.wiring.push_back(WireInfo{"a.x", "b.x", 1.0, 0.0});
    schema.wiring.push_back(WireInfo{"b.x", "c.x", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *a = find_node(graph, "a");
    const TopologyNode *b = find_node(graph, "b");
    const TopologyNode *c = find_node(graph, "c");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(a->position.x, 0.0f);
    EXPECT_FLOAT_EQ(b->position.x, 300.0f);
    EXPECT_FLOAT_EQ(c->position.x, 600.0f);
}

TEST(TopologyGraphLayout, SameLayerNodesAreVerticallyCenteredAndSpaced) {
    Schema schema;
    schema.modules.push_back(make_module("a", {"x"}));
    schema.modules.push_back(make_module("b", {"x"}));

    TopologyGraph graph;
    graph.build_from_schema(schema);

    ASSERT_EQ(graph.nodes().size(), 2u);
    EXPECT_FLOAT_EQ(graph.nodes()[0].position.x, 0.0f);
    EXPECT_FLOAT_EQ(graph.nodes()[1].position.x, 0.0f);
    EXPECT_FLOAT_EQ(graph.nodes()[0].position.y, -100.0f);
    EXPECT_FLOAT_EQ(graph.nodes()[1].position.y, 100.0f);
}

TEST(TopologyGraphLayout, UnconnectedNodeRemainsInLayerZero) {
    Schema schema;
    schema.modules.push_back(make_module("a", {"x"}));
    schema.modules.push_back(make_module("b", {"x"}));
    schema.modules.push_back(make_module("standalone", {"x"}));
    schema.wiring.push_back(WireInfo{"a.x", "b.x", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *standalone = find_node(graph, "standalone");
    ASSERT_NE(standalone, nullptr);
    EXPECT_FLOAT_EQ(standalone->position.x, 0.0f);
}

TEST(TopologyGraphLayout, DiamondPatternPlacesSinkAtLayerTwo) {
    Schema schema;
    schema.modules.push_back(make_module("a", {"x"}));
    schema.modules.push_back(make_module("b", {"x"}));
    schema.modules.push_back(make_module("c", {"x"}));
    schema.modules.push_back(make_module("d", {"x"}));
    schema.wiring.push_back(WireInfo{"a.x", "b.x", 1.0, 0.0});
    schema.wiring.push_back(WireInfo{"a.x", "c.x", 1.0, 0.0});
    schema.wiring.push_back(WireInfo{"b.x", "d.x", 1.0, 0.0});
    schema.wiring.push_back(WireInfo{"c.x", "d.x", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *d = find_node(graph, "d");
    ASSERT_NE(d, nullptr);
    EXPECT_FLOAT_EQ(d->position.x, 600.0f);
}

TEST(TopologyGraphLayout, LayoutProducesUniqueNodePositions) {
    Schema schema;
    schema.modules.push_back(make_module("a", {"x"}));
    schema.modules.push_back(make_module("b", {"x"}));
    schema.modules.push_back(make_module("c", {"x"}));
    schema.wiring.push_back(WireInfo{"a.x", "c.x", 1.0, 0.0});
    schema.wiring.push_back(WireInfo{"b.x", "c.x", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    for (size_t i = 0; i < graph.nodes().size(); ++i) {
        for (size_t j = i + 1; j < graph.nodes().size(); ++j) {
            const bool same_x =
                std::fabs(graph.nodes()[i].position.x - graph.nodes()[j].position.x) < 1e-6f;
            const bool same_y =
                std::fabs(graph.nodes()[i].position.y - graph.nodes()[j].position.y) < 1e-6f;
            EXPECT_FALSE(same_x && same_y);
        }
    }
}
