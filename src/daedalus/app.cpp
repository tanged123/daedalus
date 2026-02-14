#include "daedalus/app.hpp"
#include "daedalus/protocol/telemetry.hpp"

#include <GLFW/glfw3.h>
#include <hello_imgui/hello_imgui.h>
#include <imgui.h>
#include <immapp/immapp.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace daedalus {

namespace {

[[nodiscard]] std::string to_lower_ascii(std::string_view input) {
    std::string lowered(input);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

[[nodiscard]] bool node_matches_filter(const data::SignalTreeNode &node,
                                       std::string_view normalized_filter) {
    if (normalized_filter.empty()) {
        return true;
    }

    const std::string lowered_name = to_lower_ascii(node.name);
    if (lowered_name.find(normalized_filter) != std::string::npos) {
        return true;
    }

    const std::string lowered_path = to_lower_ascii(node.full_path);
    if (lowered_path.find(normalized_filter) != std::string::npos) {
        return true;
    }

    for (const auto &child : node.children) {
        if (node_matches_filter(*child, normalized_filter)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_reset_message(const nlohmann::json &msg) {
    const std::string type = msg.value("type", "");
    if (type == "event") {
        return msg.value("event", "") == "reset";
    }
    if (type == "ack") {
        return msg.value("action", "") == "reset";
    }
    return false;
}

} // namespace

App::App() = default;
App::~App() = default;

int App::run(int /*argc*/, char * /*argv*/[]) {
    // Register GLFW error callback before initialization for diagnostics.
    // This is safe to call before glfwInit() (Hello ImGui handles that).
    glfwSetErrorCallback([](int error, const char *description) {
        std::fprintf(stderr, "[GLFW Error %d] %s\n", error, description);
    });

    // Force X11 on WSL2/WSLg — GLFW 3.4 prefers Wayland when
    // WAYLAND_DISPLAY is set, but WSLg's Wayland EGL is unreliable.
    // Unsetting WAYLAND_DISPLAY is the most reliable approach since
    // Hello ImGui may override glfwInitHint during its own setup.
    if (std::getenv("WSL_DISTRO_NAME") != nullptr && std::getenv("GLFW_PLATFORM") == nullptr) {
        unsetenv("WAYLAND_DISPLAY");
    }

    // Create Hermes client
    client_ = std::make_unique<protocol::HermesClient>(server_url_);
    plot_manager_.set_signal_unit_lookup(
        [this](const std::string &signal_path) -> std::optional<std::string> {
            const auto it = signal_units_.find(signal_path);
            if (it == signal_units_.end()) {
                return std::nullopt;
            }
            return it->second;
        });
    console_view_.set_replay_callback(
        [this](const std::string &action, const nlohmann::json &params) {
            if (action.empty()) {
                return;
            }
            client_->send_command(action, params);
            console_log_.add_command(action, params);
        });

    // Set up Hello ImGui runner params
    HelloImGui::RunnerParams runner_params;
    runner_params.appWindowParams.windowTitle = "Daedalus";
    runner_params.appWindowParams.windowGeometry.size = {1280, 720};

    // Enable docking
    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showMenu_App = false;
    runner_params.imGuiWindowParams.showMenu_View = false;
    runner_params.imGuiWindowParams.showStatusBar = true;

    // Disable idling — we have live telemetry streaming
    runner_params.fpsIdling.enableIdling = false;

    // Define docking layout
    //  ___________________________________________
    //  |              |                           |
    //  | Signals      |    MainDockSpace          |
    //  | [Tree|Table] |    (plots)                |
    //  | (left 20%)   |                           |
    //  |              |---------------------------|
    //  |              |    Console                |
    //  |              |    (bottom 30%)           |
    //  -------------------------------------------
    HelloImGui::DockingSplit split_left;
    split_left.initialDock = "MainDockSpace";
    split_left.newDock = "SignalsSpace";
    split_left.direction = ImGuiDir_Left;
    split_left.ratio = 0.20f;

    HelloImGui::DockingSplit split_plots_console;
    split_plots_console.initialDock = "MainDockSpace";
    split_plots_console.newDock = "ConsoleSpace";
    split_plots_console.direction = ImGuiDir_Down;
    split_plots_console.ratio = 0.30f;

    runner_params.dockingParams.dockingSplits = {split_left, split_plots_console};

    // Define dockable windows
    HelloImGui::DockableWindow signals_window;
    signals_window.label = "Signals";
    signals_window.dockSpaceName = "SignalsSpace";
    signals_window.GuiFunction = [this] { render_signals(); };

    HelloImGui::DockableWindow plots_window;
    plots_window.label = "Plots";
    plots_window.dockSpaceName = "MainDockSpace";
    plots_window.GuiFunction = [this] { render_plot_workspace(); };

    HelloImGui::DockableWindow console_window;
    console_window.label = "Console";
    console_window.dockSpaceName = "ConsoleSpace";
    console_window.GuiFunction = [this] { render_console(); };

    runner_params.dockingParams.dockableWindows = {signals_window, plots_window, console_window};

    // Status bar: connection status
    runner_params.callbacks.ShowStatus = [this] { render_connection_status(); };

    // Per-frame processing: poll queues and process data
    runner_params.callbacks.BeforeImGuiRender = [this] {
        process_events();
        process_telemetry();
    };

    // Connect to Hermes on startup
    runner_params.callbacks.PostInit = [this] { client_->connect(); };

    // Disconnect on exit
    runner_params.callbacks.BeforeExit = [this] { client_->disconnect(); };

    // Run with ImmApp (includes ImPlot initialization for future use)
    ImmApp::AddOnsParams addons;
    addons.withImplot = true;
    ImmApp::Run(runner_params, addons);

    return 0;
}

void App::process_events() {
    std::string json_str;
    // Drain all queued events this frame
    while (client_->event_queue().try_pop(json_str)) {
        handle_event(json_str);
    }
}

void App::handle_event(const std::string &json_str) {
    console_log_.add_from_json(json_str, playback_state_.last_sim_time);

    auto msg = nlohmann::json::parse(json_str, nullptr, false);
    if (msg.is_discarded()) {
        return;
    }

    const std::string type = msg.value("type", "");
    bool state_changed = playback_state_.update_from_event(msg);
    if (!state_changed) {
        (void)playback_state_.update_from_ack(msg);
    }

    if (is_reset_message(msg)) {
        for (auto &[index, buffer] : signal_buffers_) {
            (void)index;
            buffer.clear();
        }
        plot_manager_.set_current_time(0.0);
    }

    if (type == "schema") {
        current_schema_ = protocol::parse_schema(msg);
        signal_tree_.build_from_schema(current_schema_);
        signal_units_.clear();
        for (const auto &module : current_schema_.modules) {
            for (const auto &signal : module.signals) {
                if (!signal.unit.has_value()) {
                    continue;
                }
                signal_units_.emplace(module.name + "." + signal.name, signal.unit.value());
            }
        }
        schema_received_ = true;

        // Auto-subscribe to all signals
        client_->subscribe({"*"});
        console_log_.add_command("subscribe", {{"signals", nlohmann::json::array({"*"})}});

    } else if (type == "ack") {
        const std::string action = msg.value("action", "");
        if (action == "subscribe") {
            auto ack = protocol::parse_subscribe_ack(msg);
            signal_tree_.update_subscription(ack);

            // Create signal buffers for each subscribed signal
            subscribed_signals_ = ack.signals;
            signal_buffers_.clear();
            for (size_t i = 0; i < ack.signals.size(); ++i) {
                signal_buffers_.emplace(i, data::SignalBuffer{});
            }
            plot_manager_.clear_panel_signals();

            // Start telemetry flow
            client_->resume();
            console_log_.add_command("resume");
        }
    } else if (type == "connection") {
        const std::string event = msg.value("event", "");
        if (event == "connected") {
            playback_state_.connected = true;
        } else if (event == "disconnected") {
            playback_state_.connected = false;
            playback_state_.reset();

            // Reset state for reconnection
            schema_received_ = false;
            subscribed_signals_.clear();
            signal_buffers_.clear();
            signal_units_.clear();
            signal_tree_.clear();
            plot_manager_.clear_panel_signals();
            signal_inspector_.reset();
        } else if (event == "error") {
            playback_state_.connected = false;
            playback_state_.reset();
        }
    }
}

void App::process_telemetry() {
    std::vector<uint8_t> frame_data;
    std::vector<double> value_storage;
    // Drain all queued telemetry frames this frame
    while (client_->telemetry_queue().try_pop(frame_data)) {
        protocol::TelemetryHeader hdr{};
        std::span<const double> values;

        if (protocol::decode_frame(frame_data.data(), frame_data.size(), hdr, values,
                                   value_storage)) {
            for (uint32_t i = 0; i < hdr.count; ++i) {
                auto it = signal_buffers_.find(i);
                if (it != signal_buffers_.end()) {
                    it->second.push(hdr.time, values[i]);
                }
            }
            plot_manager_.set_current_time(hdr.time);
            playback_state_.update_from_telemetry(hdr.frame, hdr.time);
        }
    }
}

void App::render_connection_status() {
    auto state = client_->state();
    playback_state_.connected = (state == protocol::ConnectionState::Connected);
    ImVec4 color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    const char *label = "Unknown";

    switch (state) {
    case protocol::ConnectionState::Connected:
        color = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
        label = "Connected";
        break;
    case protocol::ConnectionState::Connecting:
        color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
        label = "Connecting";
        break;
    case protocol::ConnectionState::Disconnected:
        color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        label = "Disconnected";
        break;
    case protocol::ConnectionState::Error:
        color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        label = "Error";
        break;
    default:
        color = ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        label = "Unknown";
        break;
    }

    // Colored status dot + text
    ImGui::TextColored(color, "%s", label);
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    ImGui::Text("%s", server_url_.c_str());

    if (!subscribed_signals_.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("%zu signals", subscribed_signals_.size());
    }

    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    const auto action = views::render_playback_controls(playback_state_);
    if (!action.has_value() || client_ == nullptr) {
        return;
    }

    switch (*action) {
    case views::PlaybackAction::Pause:
        client_->pause();
        console_log_.add_command("pause");
        break;
    case views::PlaybackAction::Resume:
        client_->resume();
        console_log_.add_command("resume");
        break;
    case views::PlaybackAction::Reset:
        client_->reset();
        console_log_.add_command("reset");
        break;
    case views::PlaybackAction::Step:
        client_->step(playback_state_.step_count);
        console_log_.add_command("step", {{"count", playback_state_.step_count}});
        break;
    }
}

void App::render_signals() {
    // View mode toggle
    if (ImGui::Selectable("Tree", signal_view_mode_ == SignalViewMode::Tree,
                          ImGuiSelectableFlags_None, ImVec2(40, 0))) {
        signal_view_mode_ = SignalViewMode::Tree;
    }
    ImGui::SameLine();
    if (ImGui::Selectable("Table", signal_view_mode_ == SignalViewMode::Table,
                          ImGuiSelectableFlags_None, ImVec2(40, 0))) {
        signal_view_mode_ = SignalViewMode::Table;
    }

    // Shared filter input
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##signal_filter", "Filter signals...", signal_filter_,
                             sizeof(signal_filter_));
    ImGui::Separator();

    if (!schema_received_) {
        ImGui::TextDisabled("Waiting for schema...");
        return;
    }

    if (signal_view_mode_ == SignalViewMode::Tree) {
        render_signal_tree();
    } else {
        signal_inspector_.set_filter_text(signal_filter_);
        signal_inspector_.render(subscribed_signals_, signal_buffers_, signal_units_);
    }
}

void App::render_signal_tree() {
    const bool apply_tree_open_request = tree_open_state_request_.has_value();

    const std::string normalized_filter = to_lower_ascii(std::string_view(signal_filter_));
    auto &root = signal_tree_.root();
    for (auto &child : root.children) {
        if (node_matches_filter(*child, normalized_filter)) {
            render_signal_tree_node(*child, normalized_filter);
        }
    }

    if (ImGui::BeginPopupContextWindow("signal_tree_context", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Expand all trees")) {
            tree_open_state_request_ = true;
        }
        if (ImGui::MenuItem("Collapse all trees")) {
            tree_open_state_request_ = false;
        }
        ImGui::EndPopup();
    }
    if (apply_tree_open_request) {
        tree_open_state_request_.reset();
    }
}

void App::render_signal_tree_node(const data::SignalTreeNode &node, std::string_view filter) {
    ImGui::PushID(node.full_path.c_str());

    if (node.is_leaf) {
        // Leaf node: show signal name + current value
        ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                        ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::TreeNodeEx(node.name.c_str(), leaf_flags);

        // Drag source + double-click quick-add for plotting.
        if (node.signal_index.has_value()) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                views::DragDropSignalPayload payload{};
                payload.buffer_index = node.signal_index.value();
                std::snprintf(payload.label, sizeof(payload.label), "%s", node.full_path.c_str());
                ImGui::SetDragDropPayload(views::kDndSignalPayloadType, &payload, sizeof(payload));
                ImGui::TextUnformatted(node.full_path.c_str());
                ImGui::EndDragDropSource();
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                plot_manager_.add_signal_to_active_or_new_panel(node.signal_index.value(),
                                                                node.full_path);
            }
        }

        // Show current value on the same line
        if (node.signal_index.has_value()) {
            auto it = signal_buffers_.find(node.signal_index.value());
            if (it != signal_buffers_.end() && !it->second.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%.4f", it->second.last_value());
            }
        }
    } else {
        // Internal node: expandable tree
        if (tree_open_state_request_.has_value()) {
            ImGui::SetNextItemOpen(tree_open_state_request_.value(), ImGuiCond_Always);
        }
        bool open = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (open) {
            for (auto &child : node.children) {
                if (node_matches_filter(*child, filter)) {
                    render_signal_tree_node(*child, filter);
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::PopID();
}

void App::render_plot_workspace() {
    plot_manager_.render_toolbar();
    plot_manager_.render(signal_buffers_);
}

void App::render_console() { console_view_.render(console_log_); }

} // namespace daedalus
