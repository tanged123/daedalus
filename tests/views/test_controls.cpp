#include "daedalus/views/controls.hpp"

#include <gtest/gtest.h>

using namespace daedalus::views;

TEST(PlaybackState, InitialState) {
    PlaybackState state;
    EXPECT_EQ(state.sim_state, SimulationState::Unknown);
    EXPECT_EQ(state.last_frame, 0u);
    EXPECT_DOUBLE_EQ(state.last_sim_time, 0.0);
    EXPECT_EQ(state.step_count, 1);
    EXPECT_FALSE(state.connected);
}

TEST(PlaybackState, UpdateFromRunningEvent) {
    PlaybackState state;
    EXPECT_TRUE(state.update_from_event({{"type", "event"}, {"event", "running"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Running);
}

TEST(PlaybackState, UpdateFromPausedEvent) {
    PlaybackState state;
    EXPECT_TRUE(state.update_from_event({{"type", "event"}, {"event", "paused"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
}

TEST(PlaybackState, UpdateFromResumeAckSetsRunning) {
    PlaybackState state;
    EXPECT_TRUE(state.update_from_ack({{"type", "ack"}, {"action", "resume"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Running);
}

TEST(PlaybackState, UpdateFromPauseAckSetsPaused) {
    PlaybackState state;
    state.sim_state = SimulationState::Running;
    EXPECT_TRUE(state.update_from_ack({{"type", "ack"}, {"action", "pause"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
}

TEST(PlaybackState, UpdateFromResetAckClearsFrameAndTime) {
    PlaybackState state;
    state.sim_state = SimulationState::Running;
    state.last_frame = 77;
    state.last_sim_time = 5.5;
    EXPECT_TRUE(state.update_from_ack({{"type", "ack"}, {"action", "reset"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
    EXPECT_EQ(state.last_frame, 0u);
    EXPECT_DOUBLE_EQ(state.last_sim_time, 0.0);
}

TEST(PlaybackState, ResetEventClearsFrameAndTimeAndSetsPaused) {
    PlaybackState state;
    state.sim_state = SimulationState::Running;
    state.last_frame = 42;
    state.last_sim_time = 9.5;

    EXPECT_TRUE(state.update_from_event({{"type", "event"}, {"event", "reset"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
    EXPECT_EQ(state.last_frame, 0u);
    EXPECT_DOUBLE_EQ(state.last_sim_time, 0.0);
}

TEST(PlaybackState, NonEventMessageDoesNotChangeState) {
    PlaybackState state;
    state.sim_state = SimulationState::Paused;

    EXPECT_FALSE(state.update_from_event({{"type", "ack"}, {"action", "pause"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
}

TEST(PlaybackState, NonAckMessageDoesNotChangeStateViaAckPath) {
    PlaybackState state;
    state.sim_state = SimulationState::Paused;
    EXPECT_FALSE(state.update_from_ack({{"type", "event"}, {"event", "running"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
}

TEST(PlaybackState, UnknownEventDoesNotChangeState) {
    PlaybackState state;
    state.sim_state = SimulationState::Paused;

    EXPECT_FALSE(state.update_from_event({{"type", "event"}, {"event", "foo"}}));
    EXPECT_EQ(state.sim_state, SimulationState::Paused);
}

TEST(PlaybackState, ResetRestoresUnknownDefaults) {
    PlaybackState state;
    state.sim_state = SimulationState::Running;
    state.last_frame = 100;
    state.last_sim_time = 44.0;
    state.step_count = 15;

    state.reset();

    EXPECT_EQ(state.sim_state, SimulationState::Unknown);
    EXPECT_EQ(state.last_frame, 0u);
    EXPECT_DOUBLE_EQ(state.last_sim_time, 0.0);
    EXPECT_EQ(state.step_count, 1);
}

TEST(PlaybackState, CanPauseOnlyWhenConnectedAndRunning) {
    PlaybackState state;
    state.connected = true;
    state.sim_state = SimulationState::Running;
    EXPECT_TRUE(state.can_pause());

    state.sim_state = SimulationState::Paused;
    EXPECT_FALSE(state.can_pause());

    state.connected = false;
    state.sim_state = SimulationState::Running;
    EXPECT_FALSE(state.can_pause());
}

TEST(PlaybackState, CanResumeWhenConnectedAndNotRunning) {
    PlaybackState state;
    state.connected = true;
    state.sim_state = SimulationState::Paused;
    EXPECT_TRUE(state.can_resume());

    state.sim_state = SimulationState::Unknown;
    EXPECT_TRUE(state.can_resume());

    state.sim_state = SimulationState::Running;
    EXPECT_FALSE(state.can_resume());
}

TEST(PlaybackState, CanStepWhenConnectedAndNotRunning) {
    PlaybackState state;
    state.connected = true;
    state.sim_state = SimulationState::Paused;
    EXPECT_TRUE(state.can_step());

    state.sim_state = SimulationState::Unknown;
    EXPECT_TRUE(state.can_step());

    state.sim_state = SimulationState::Running;
    EXPECT_FALSE(state.can_step());
}

TEST(PlaybackState, ControlsDisabledWhenDisconnected) {
    PlaybackState state;
    state.connected = false;
    state.sim_state = SimulationState::Running;

    EXPECT_FALSE(state.controls_enabled());
    EXPECT_FALSE(state.can_pause());
    EXPECT_FALSE(state.can_resume());
    EXPECT_FALSE(state.can_reset());
    EXPECT_FALSE(state.can_step());
}

TEST(PlaybackState, UpdateFromTelemetryStoresFrameAndTime) {
    PlaybackState state;
    state.update_from_telemetry(1234, 12.34);
    EXPECT_EQ(state.last_frame, 1234u);
    EXPECT_DOUBLE_EQ(state.last_sim_time, 12.34);
}

TEST(PlaybackState, SimulationStateLabelMatchesEnum) {
    EXPECT_STREQ(simulation_state_label(SimulationState::Unknown), "Unknown");
    EXPECT_STREQ(simulation_state_label(SimulationState::Running), "Running");
    EXPECT_STREQ(simulation_state_label(SimulationState::Paused), "Paused");
}
