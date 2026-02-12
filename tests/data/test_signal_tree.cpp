#include "daedalus/data/signal_tree.hpp"

#include <gtest/gtest.h>

using namespace daedalus::data;
using namespace daedalus::protocol;

namespace {

Schema make_test_schema() {
    Schema schema;

    ModuleInfo vehicle;
    vehicle.name = "vehicle";
    vehicle.signals = {
        {"position.x", "f64", "m"},
        {"position.y", "f64", "m"},
        {"velocity.x", "f64", "m/s"},
        {"velocity.y", "f64", "m/s"},
    };
    schema.modules.push_back(std::move(vehicle));

    ModuleInfo inputs;
    inputs.name = "inputs";
    inputs.signals = {
        {"throttle", "f64", std::nullopt},
    };
    schema.modules.push_back(std::move(inputs));

    return schema;
}

} // namespace

TEST(SignalTree, BuildFromSchema) {
    SignalTree tree;
    tree.build_from_schema(make_test_schema());

    auto &root = tree.root();
    // Root should have 2 children: "vehicle" and "inputs"
    EXPECT_EQ(root.children.size(), 2u);
}

TEST(SignalTree, Hierarchy) {
    SignalTree tree;
    tree.build_from_schema(make_test_schema());

    // vehicle.position.x should exist
    auto *node = tree.find("vehicle.position.x");
    ASSERT_NE(node, nullptr);
    EXPECT_TRUE(node->is_leaf);
    EXPECT_EQ(node->name, "x");
    EXPECT_EQ(node->full_path, "vehicle.position.x");

    // vehicle.position should exist as internal node
    auto *pos = tree.find("vehicle.position");
    ASSERT_NE(pos, nullptr);
    EXPECT_FALSE(pos->is_leaf);
    EXPECT_EQ(pos->children.size(), 2u); // x, y
}

TEST(SignalTree, FindNonExistent) {
    SignalTree tree;
    tree.build_from_schema(make_test_schema());

    EXPECT_EQ(tree.find("nonexistent"), nullptr);
    EXPECT_EQ(tree.find("vehicle.nonexistent"), nullptr);
}

TEST(SignalTree, AllSignals) {
    SignalTree tree;
    tree.build_from_schema(make_test_schema());

    auto signals = tree.all_signals();
    ASSERT_EQ(signals.size(), 5u);

    // Sorted alphabetically
    EXPECT_EQ(signals[0], "inputs.throttle");
    EXPECT_EQ(signals[1], "vehicle.position.x");
    EXPECT_EQ(signals[2], "vehicle.position.y");
    EXPECT_EQ(signals[3], "vehicle.velocity.x");
    EXPECT_EQ(signals[4], "vehicle.velocity.y");
}

TEST(SignalTree, UpdateSubscription) {
    SignalTree tree;
    tree.build_from_schema(make_test_schema());

    SubscribeAck ack;
    ack.count = 3;
    ack.signals = {"vehicle.velocity.y", "vehicle.position.x", "inputs.throttle"};
    tree.update_subscription(ack);

    auto *vy = tree.find("vehicle.velocity.y");
    ASSERT_NE(vy, nullptr);
    ASSERT_TRUE(vy->signal_index.has_value());
    EXPECT_EQ(vy->signal_index.value(), 0u);

    auto *px = tree.find("vehicle.position.x");
    ASSERT_NE(px, nullptr);
    ASSERT_TRUE(px->signal_index.has_value());
    EXPECT_EQ(px->signal_index.value(), 1u);

    auto *throttle = tree.find("inputs.throttle");
    ASSERT_NE(throttle, nullptr);
    ASSERT_TRUE(throttle->signal_index.has_value());
    EXPECT_EQ(throttle->signal_index.value(), 2u);

    // Unsubscribed signal should have no index
    auto *py = tree.find("vehicle.position.y");
    ASSERT_NE(py, nullptr);
    EXPECT_FALSE(py->signal_index.has_value());
}

TEST(SignalTree, EmptySchema) {
    SignalTree tree;
    Schema empty;
    tree.build_from_schema(empty);

    EXPECT_TRUE(tree.root().children.empty());
    EXPECT_TRUE(tree.all_signals().empty());
}

TEST(SignalTree, Clear) {
    SignalTree tree;
    tree.build_from_schema(make_test_schema());
    EXPECT_FALSE(tree.all_signals().empty());

    tree.clear();
    EXPECT_TRUE(tree.root().children.empty());
    EXPECT_TRUE(tree.all_signals().empty());
    EXPECT_EQ(tree.find("vehicle.position.x"), nullptr);
}
