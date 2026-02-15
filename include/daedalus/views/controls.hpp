#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace daedalus::views {

/// Current simulation state, determined from Hermes events.
enum class SimulationState {
    Unknown,
    Running,
    Paused,
};

/// Converts SimulationState to a display label.
const char *simulation_state_label(SimulationState state);

/// Playback state and controls data model.
struct PlaybackState {
    SimulationState sim_state = SimulationState::Unknown;
    uint64_t last_frame = 0;
    double last_sim_time = 0.0;
    int step_count = 1;
    bool connected = false;

    [[nodiscard]] bool update_from_event(const nlohmann::json &msg);
    [[nodiscard]] bool update_from_ack(const nlohmann::json &msg);
    void update_from_telemetry(uint64_t frame, double sim_time);
    void reset();

    [[nodiscard]] bool controls_enabled() const { return connected; }
    [[nodiscard]] bool can_pause() const {
        return connected && sim_state == SimulationState::Running;
    }
    [[nodiscard]] bool can_resume() const {
        return connected && sim_state != SimulationState::Running;
    }
    [[nodiscard]] bool can_reset() const { return connected; }
    [[nodiscard]] bool can_step() const {
        return connected && sim_state != SimulationState::Running;
    }
};

/// User action emitted by the playback controls.
enum class PlaybackAction {
    Pause,
    Resume,
    Reset,
    Step,
};

/// Render playback controls in the status bar and emit a selected action.
std::optional<PlaybackAction> render_playback_controls(PlaybackState &state);

} // namespace daedalus::views
