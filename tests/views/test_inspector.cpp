#include "daedalus/views/inspector.hpp"

#include <gtest/gtest.h>

namespace {

daedalus::data::SignalBuffer make_buffer(double value, double time = 1.0) {
    daedalus::data::SignalBuffer buffer;
    buffer.push(time, value);
    return buffer;
}

} // namespace

using daedalus::views::InspectorSortColumn;
using daedalus::views::SignalInspector;

TEST(SignalInspector, SortBySignalAscendingAndDescending) {
    SignalInspector inspector;
    const std::vector<std::string> signals = {"zeta.x", "alpha.x", "beta.x"};
    const std::map<size_t, daedalus::data::SignalBuffer> buffers = {
        {0, make_buffer(3.0)},
        {1, make_buffer(1.0)},
        {2, make_buffer(2.0)},
    };
    const std::unordered_map<std::string, std::string> units = {};

    inspector.set_sort(InspectorSortColumn::Signal, true);
    const auto ascending = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(ascending.size(), 3u);
    EXPECT_EQ(ascending[0], 1u);
    EXPECT_EQ(ascending[1], 2u);
    EXPECT_EQ(ascending[2], 0u);

    inspector.set_sort(InspectorSortColumn::Signal, false);
    const auto descending = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(descending.size(), 3u);
    EXPECT_EQ(descending[0], 0u);
    EXPECT_EQ(descending[1], 2u);
    EXPECT_EQ(descending[2], 1u);
}

TEST(SignalInspector, SortByValueAscendingAndDescending) {
    SignalInspector inspector;
    const std::vector<std::string> signals = {"a", "b", "c"};
    const std::map<size_t, daedalus::data::SignalBuffer> buffers = {
        {0, make_buffer(10.0)},
        {1, make_buffer(2.0)},
        {2, make_buffer(5.0)},
    };
    const std::unordered_map<std::string, std::string> units = {};

    inspector.set_sort(InspectorSortColumn::Value, true);
    const auto ascending = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(ascending.size(), 3u);
    EXPECT_EQ(ascending[0], 1u);
    EXPECT_EQ(ascending[1], 2u);
    EXPECT_EQ(ascending[2], 0u);

    inspector.set_sort(InspectorSortColumn::Value, false);
    const auto descending = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(descending.size(), 3u);
    EXPECT_EQ(descending[0], 0u);
    EXPECT_EQ(descending[1], 2u);
    EXPECT_EQ(descending[2], 1u);
}

TEST(SignalInspector, SortByUnit) {
    SignalInspector inspector;
    const std::vector<std::string> signals = {"sig3", "sig1", "sig2"};
    const std::map<size_t, daedalus::data::SignalBuffer> buffers = {
        {0, make_buffer(3.0)},
        {1, make_buffer(1.0)},
        {2, make_buffer(2.0)},
    };
    const std::unordered_map<std::string, std::string> units = {
        {"sig1", "m"},
        {"sig2", "deg"},
        {"sig3", "s"},
    };

    inspector.set_sort(InspectorSortColumn::Unit, true);
    const auto indices = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(indices.size(), 3u);
    EXPECT_EQ(indices[0], 2u); // deg
    EXPECT_EQ(indices[1], 1u); // m
    EXPECT_EQ(indices[2], 0u); // s
}

TEST(SignalInspector, FilterHidesNonMatchingSignals) {
    SignalInspector inspector;
    const std::vector<std::string> signals = {"vehicle.pos.x", "vehicle.pos.y", "imu.roll"};
    const std::map<size_t, daedalus::data::SignalBuffer> buffers = {
        {0, make_buffer(1.0)},
        {1, make_buffer(2.0)},
        {2, make_buffer(3.0)},
    };
    const std::unordered_map<std::string, std::string> units = {};

    inspector.set_filter_text("pos");
    const auto indices = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(indices.size(), 2u);
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);
}

TEST(SignalInspector, ResetClearsFilterAndRestoresDefaultSort) {
    SignalInspector inspector;
    const std::vector<std::string> signals = {"z", "a", "m"};
    const std::map<size_t, daedalus::data::SignalBuffer> buffers = {
        {0, make_buffer(9.0)},
        {1, make_buffer(3.0)},
        {2, make_buffer(6.0)},
    };
    const std::unordered_map<std::string, std::string> units = {};

    inspector.set_sort(InspectorSortColumn::Value, false);
    inspector.set_filter_text("a");
    auto filtered = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0], 1u);

    inspector.reset();
    const auto reset_indices = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(reset_indices.size(), 3u);
    EXPECT_EQ(reset_indices[0], 1u);
    EXPECT_EQ(reset_indices[1], 2u);
    EXPECT_EQ(reset_indices[2], 0u);
}

TEST(SignalInspector, EmptySignalsProducesNoRows) {
    SignalInspector inspector;
    const std::vector<std::string> signals;
    const std::map<size_t, daedalus::data::SignalBuffer> buffers;
    const std::unordered_map<std::string, std::string> units;

    const auto indices = inspector.build_visible_indices(signals, buffers, units);
    EXPECT_TRUE(indices.empty());
}

TEST(SignalInspector, MissingBufferSortsAfterSignalsWithValuesWhenSortingByValue) {
    SignalInspector inspector;
    const std::vector<std::string> signals = {"has.value", "missing.value", "has.value.2"};
    const std::map<size_t, daedalus::data::SignalBuffer> buffers = {
        {0, make_buffer(2.0)},
        {2, make_buffer(5.0)},
    };
    const std::unordered_map<std::string, std::string> units = {};

    inspector.set_sort(InspectorSortColumn::Value, true);
    const auto indices = inspector.build_visible_indices(signals, buffers, units);
    ASSERT_EQ(indices.size(), 3u);
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 2u);
    EXPECT_EQ(indices[2], 1u);
}
