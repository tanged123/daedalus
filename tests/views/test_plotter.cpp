#include "daedalus/views/plotter.hpp"

#include <gtest/gtest.h>

using namespace daedalus::views;

TEST(PlotPanel, DefaultState) {
    PlotPanel panel;
    EXPECT_TRUE(panel.live_mode);
    EXPECT_FLOAT_EQ(panel.history_seconds, 10.0f);
    EXPECT_FLOAT_EQ(panel.plot_height, 260.0f);
    EXPECT_FLOAT_EQ(panel.y_padding_percent, 5.0f);
    EXPECT_TRUE(panel.auto_fit_y1);
    EXPECT_TRUE(panel.auto_fit_y2);
    EXPECT_TRUE(panel.auto_fit_y3);
    EXPECT_FALSE(panel.show_y2);
    EXPECT_FALSE(panel.show_y3);
    EXPECT_FALSE(panel.show_cursor);
    EXPECT_FALSE(panel.cursor_initialized);
    EXPECT_FALSE(panel.show_stats);
}

TEST(PlotPanel, AddSignal) {
    PlotPanel panel;
    EXPECT_TRUE(panel.add_signal(5, "vehicle.position.x"));
    ASSERT_EQ(panel.signals.size(), 1u);
    EXPECT_EQ(panel.signals[0].buffer_index, 5u);
    EXPECT_EQ(panel.signals[0].label, "vehicle.position.x");
    EXPECT_EQ(panel.signals[0].y_axis, ImAxis_Y1);
}

TEST(PlotPanel, AddDuplicateSignalRejected) {
    PlotPanel panel;
    EXPECT_TRUE(panel.add_signal(5, "vehicle.position.x"));
    EXPECT_FALSE(panel.add_signal(5, "vehicle.position.x"));
    EXPECT_EQ(panel.signals.size(), 1u);
}

TEST(PlotPanel, RemoveSignal) {
    PlotPanel panel;
    panel.add_signal(1, "a");
    panel.add_signal(2, "b");
    panel.remove_signal(1);
    ASSERT_EQ(panel.signals.size(), 1u);
    EXPECT_EQ(panel.signals[0].buffer_index, 2u);
}

TEST(PlotPanel, HasSignalsOnAxis) {
    PlotPanel panel;
    panel.add_signal(1, "a", ImAxis_Y1);
    panel.add_signal(2, "b", ImAxis_Y2);
    EXPECT_TRUE(panel.has_signals_on(ImAxis_Y1));
    EXPECT_TRUE(panel.has_signals_on(ImAxis_Y2));
    EXPECT_FALSE(panel.has_signals_on(ImAxis_Y3));
}

TEST(PlotPanel, SetSignalAxis) {
    PlotPanel panel;
    panel.add_signal(1, "a", ImAxis_Y1);
    EXPECT_TRUE(panel.set_signal_axis(1, ImAxis_Y3));
    EXPECT_EQ(panel.signals[0].y_axis, ImAxis_Y3);
    EXPECT_FALSE(panel.set_signal_axis(999, ImAxis_Y2));
}

TEST(PlotManager, CreatePanelIncrementsCount) {
    PlotManager manager;
    EXPECT_EQ(manager.panel_count(), 0u);
    const size_t index = manager.create_panel();
    EXPECT_EQ(index, 0u);
    EXPECT_EQ(manager.panel_count(), 1u);
}

TEST(PlotManager, RemovePanelDecrementsCount) {
    PlotManager manager;
    manager.create_panel();
    manager.create_panel();
    manager.remove_panel(0);
    EXPECT_EQ(manager.panel_count(), 1u);
}

TEST(PlotManager, CreatePanelsHaveUniqueIds) {
    PlotManager manager;
    manager.create_panel();
    manager.create_panel();
    EXPECT_NE(manager.panel(0).id, manager.panel(1).id);
}

TEST(PlotManager, ClearRemovesPanels) {
    PlotManager manager;
    manager.create_panel();
    manager.create_panel();
    manager.clear();
    EXPECT_EQ(manager.panel_count(), 0u);
}

TEST(PlotManager, CurrentTimeRoundTrip) {
    PlotManager manager;
    manager.set_current_time(123.456);
    EXPECT_DOUBLE_EQ(manager.current_time(), 123.456);
}

TEST(PlotManager, AddSignalToActiveOrNewPanelCreatesPanel) {
    PlotManager manager;
    EXPECT_TRUE(manager.add_signal_to_active_or_new_panel(3, "vehicle.velocity.x"));
    ASSERT_EQ(manager.panel_count(), 1u);
    ASSERT_EQ(manager.panel(0).signals.size(), 1u);
    EXPECT_EQ(manager.panel(0).signals[0].buffer_index, 3u);
}

TEST(PlotManager, AddSignalToPanelRejectsInvalidPanelIndex) {
    PlotManager manager;
    EXPECT_FALSE(manager.add_signal_to_panel(99, 1, "vehicle.accel.x"));
}

TEST(PlotManager, AddSignalToPanelRejectsDuplicatesInSamePanel) {
    PlotManager manager;
    const size_t panel_index = manager.create_panel();
    EXPECT_TRUE(manager.add_signal_to_panel(panel_index, 1, "vehicle.att.roll"));
    EXPECT_FALSE(manager.add_signal_to_panel(panel_index, 1, "vehicle.att.roll"));
}

TEST(PlotManager, AddSignalToPanelEnablesAuxAxesAndTracksActivePanel) {
    PlotManager manager;
    const size_t panel_index = manager.create_panel();

    EXPECT_TRUE(manager.add_signal_to_panel(panel_index, 1, "vehicle.att.roll", ImAxis_Y2));
    EXPECT_TRUE(manager.add_signal_to_panel(panel_index, 2, "vehicle.att.yaw", ImAxis_Y3));

    const auto &panel = manager.panel(panel_index);
    EXPECT_TRUE(panel.show_y2);
    EXPECT_TRUE(panel.show_y3);
    ASSERT_TRUE(manager.active_panel_index().has_value());
    EXPECT_EQ(*manager.active_panel_index(), panel_index);
}

TEST(PlotManager, ClearPanelSignalsClearsAssignmentsAndAuxAxes) {
    PlotManager manager;
    const size_t panel_index = manager.create_panel();
    ASSERT_TRUE(manager.add_signal_to_panel(panel_index, 1, "vehicle.pos.x", ImAxis_Y2));
    ASSERT_TRUE(manager.add_signal_to_panel(panel_index, 2, "vehicle.pos.y", ImAxis_Y3));

    manager.clear_panel_signals();

    const auto &panel = manager.panel(panel_index);
    EXPECT_TRUE(panel.signals.empty());
    EXPECT_FALSE(panel.show_y2);
    EXPECT_FALSE(panel.show_y3);
}

TEST(PlotManager, SetAllLiveReenablesPanelsAndResetsCursorTime) {
    PlotManager manager;
    manager.set_current_time(42.0);
    const size_t panel_index = manager.create_panel();
    auto &panel = manager.panel(panel_index);
    panel.live_mode = false;
    panel.cursor_time = 0.0;

    manager.set_all_live();

    EXPECT_TRUE(panel.live_mode);
    EXPECT_DOUBLE_EQ(panel.cursor_time, 42.0);
}

TEST(PlotManager, RemovePanelUpdatesActivePanelIndex) {
    PlotManager manager;
    manager.create_panel(); // 0
    manager.create_panel(); // 1
    manager.create_panel(); // 2 (active)
    ASSERT_TRUE(manager.active_panel_index().has_value());
    EXPECT_EQ(*manager.active_panel_index(), 2u);

    manager.remove_panel(1);
    ASSERT_TRUE(manager.active_panel_index().has_value());
    EXPECT_EQ(*manager.active_panel_index(), 1u);

    manager.remove_panel(1);
    EXPECT_FALSE(manager.active_panel_index().has_value());
}

TEST(PlotManager, RemovePanelOutOfRangeIsNoOp) {
    PlotManager manager;
    manager.create_panel();
    manager.remove_panel(99);
    EXPECT_EQ(manager.panel_count(), 1u);
}

TEST(PlotManager, ClearResetsPanelIdCounter) {
    PlotManager manager;
    manager.create_panel();
    manager.create_panel();
    manager.clear();
    const size_t index = manager.create_panel();
    EXPECT_EQ(index, 0u);
    EXPECT_EQ(manager.panel(0).id, "Plot 0");
}

TEST(PlotManager, CreatePanelUsesProvidedTitle) {
    PlotManager manager;
    const size_t index = manager.create_panel("Attitude");
    EXPECT_EQ(manager.panel(index).title, "Attitude");
}

TEST(PlotManager, AddSignalToActivePanelDoesNotCreateExtraPanel) {
    PlotManager manager;
    const size_t first = manager.create_panel();
    EXPECT_TRUE(manager.add_signal_to_active_or_new_panel(3, "vehicle.velocity.x"));
    EXPECT_TRUE(manager.add_signal_to_active_or_new_panel(4, "vehicle.velocity.y"));
    EXPECT_EQ(manager.panel_count(), 1u);
    EXPECT_EQ(manager.panel(first).signals.size(), 2u);
}

TEST(PlotManager, CanSetSignalUnitLookupCallback) {
    PlotManager manager;
    manager.set_signal_unit_lookup([](const std::string &label) -> std::optional<std::string> {
        if (label == "vehicle.position.x") {
            return "m";
        }
        return std::nullopt;
    });
    SUCCEED();
}
