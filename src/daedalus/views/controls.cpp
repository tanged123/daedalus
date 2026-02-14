#include "daedalus/views/controls.hpp"

#include <imgui.h>

#include <algorithm>

namespace daedalus::views {

const char *simulation_state_label(SimulationState state) {
    switch (state) {
    case SimulationState::Running:
        return "Running";
    case SimulationState::Paused:
        return "Paused";
    case SimulationState::Unknown:
    default:
        return "Unknown";
    }
}

bool PlaybackState::update_from_event(const nlohmann::json &msg) {
    if (msg.value("type", "") != "event") {
        return false;
    }

    const std::string event = msg.value("event", "");
    const SimulationState previous = sim_state;

    if (event == "running") {
        sim_state = SimulationState::Running;
    } else if (event == "paused") {
        sim_state = SimulationState::Paused;
    } else if (event == "reset") {
        sim_state = SimulationState::Paused;
        last_frame = 0;
        last_sim_time = 0.0;
    } else {
        return false;
    }

    return previous != sim_state;
}

bool PlaybackState::update_from_ack(const nlohmann::json &msg) {
    if (msg.value("type", "") != "ack") {
        return false;
    }

    const std::string action = msg.value("action", "");
    const SimulationState previous = sim_state;

    if (action == "resume") {
        sim_state = SimulationState::Running;
    } else if (action == "pause") {
        sim_state = SimulationState::Paused;
    } else if (action == "reset") {
        sim_state = SimulationState::Paused;
        last_frame = 0;
        last_sim_time = 0.0;
    } else if (action == "step") {
        // Step executes while paused and should keep paused semantics.
        sim_state = SimulationState::Paused;
    } else {
        return false;
    }

    return previous != sim_state;
}

void PlaybackState::update_from_telemetry(uint64_t frame, double sim_time) {
    last_frame = frame;
    last_sim_time = sim_time;
}

void PlaybackState::reset() {
    sim_state = SimulationState::Unknown;
    last_frame = 0;
    last_sim_time = 0.0;
    step_count = 1;
}

std::optional<PlaybackAction> render_playback_controls(PlaybackState &state) {
    std::optional<PlaybackAction> action;

    ImGui::BeginDisabled(!state.can_resume());
    if (ImGui::SmallButton("Resume")) {
        action = PlaybackAction::Resume;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!state.can_pause());
    if (ImGui::SmallButton("Pause")) {
        action = PlaybackAction::Pause;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!state.can_reset());
    if (ImGui::SmallButton("Reset")) {
        action = PlaybackAction::Reset;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!state.can_step());
    ImGui::SetNextItemWidth(52.0f);
    ImGui::InputInt("##step_count", &state.step_count, 0, 0);
    state.step_count = std::clamp(state.step_count, 1, 10000);
    ImGui::SameLine();
    if (ImGui::SmallButton("Step")) {
        action = PlaybackAction::Step;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    ImVec4 state_color = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    if (state.sim_state == SimulationState::Running) {
        state_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
    } else if (state.sim_state == SimulationState::Paused) {
        state_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
    }
    ImGui::TextColored(state_color, "%s", simulation_state_label(state.sim_state));

    ImGui::SameLine();
    ImGui::TextDisabled("F:%llu t:%.2fs", static_cast<unsigned long long>(state.last_frame),
                        state.last_sim_time);

    return action;
}

} // namespace daedalus::views
