#include "daedalus/app.hpp"
#include "daedalus/protocol/telemetry.hpp"

#include <hello_imgui/hello_imgui.h>
#include <imgui.h>
#include <immapp/immapp.h>
#include <nlohmann/json.hpp>

#include <cstdio>

namespace daedalus {

App::App() = default;
App::~App() = default;

int App::run(int /*argc*/, char * /*argv*/[]) {
    // Create Hermes client
    client_ = std::make_unique<protocol::HermesClient>(server_url_);

    // Set up Hello ImGui runner params
    HelloImGui::RunnerParams runner_params;
    runner_params.appWindowParams.windowTitle = "Daedalus";
    runner_params.appWindowParams.windowGeometry.size = {1280, 720};

    // Enable docking
    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;

    // Disable idling — we have live telemetry streaming
    runner_params.fpsIdling.enableIdling = false;

    // Define docking layout
    //  ___________________________________________
    //  |              |                           |
    //  | Signal Tree  |    MainDockSpace          |
    //  | (left 25%)   |    (future: plotter)      |
    //  |              |                           |
    //  |              |                           |
    //  -------------------------------------------
    HelloImGui::DockingSplit split_left;
    split_left.initialDock = "MainDockSpace";
    split_left.newDock = "SignalTreeSpace";
    split_left.direction = ImGuiDir_Left;
    split_left.ratio = 0.25f;
    runner_params.dockingParams.dockingSplits = {split_left};

    // Define dockable windows
    HelloImGui::DockableWindow signal_tree_window;
    signal_tree_window.label = "Signal Tree";
    signal_tree_window.dockSpaceName = "SignalTreeSpace";
    signal_tree_window.GuiFunction = [this] { render_signal_tree(); };

    runner_params.dockingParams.dockableWindows = {signal_tree_window};

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
    try {
        auto msg = nlohmann::json::parse(json_str);
        std::string type = msg.value("type", "");

        if (type == "schema") {
            current_schema_ = protocol::parse_schema(msg);
            signal_tree_.build_from_schema(current_schema_);
            schema_received_ = true;

            // Auto-subscribe to all signals
            client_->subscribe({"*"});
            std::printf("[Daedalus] Schema received: %zu modules\n",
                        current_schema_.modules.size());

        } else if (type == "ack") {
            std::string action = msg.value("action", "");
            if (action == "subscribe") {
                auto ack = protocol::parse_subscribe_ack(msg);
                signal_tree_.update_subscription(ack);

                // Create signal buffers for each subscribed signal
                subscribed_signals_ = ack.signals;
                signal_buffers_.clear();
                for (size_t i = 0; i < ack.signals.size(); ++i) {
                    signal_buffers_.emplace(i, data::SignalBuffer{});
                }

                std::printf("[Daedalus] Subscribed to %u signals\n", ack.count);

                // Start telemetry flow
                client_->resume();
            }
        } else if (type == "event") {
            std::string event = msg.value("event", "");
            std::printf("[Daedalus] Event: %s\n", event.c_str());

        } else if (type == "error") {
            std::string message = msg.value("message", "");
            std::fprintf(stderr, "[Daedalus] Error: %s\n", message.c_str());

        } else if (type == "connection") {
            std::string event = msg.value("event", "");
            std::printf("[Daedalus] Connection: %s\n", event.c_str());

            if (event == "disconnected") {
                // Reset state for reconnection
                schema_received_ = false;
                signal_buffers_.clear();
                signal_tree_.clear();
            }
        }
    } catch (const std::exception &e) {
        std::fprintf(stderr, "[Daedalus] Failed to parse event: %s\n", e.what());
    }
}

void App::process_telemetry() {
    std::vector<uint8_t> frame_data;
    // Drain all queued telemetry frames this frame
    while (client_->telemetry_queue().try_pop(frame_data)) {
        protocol::TelemetryHeader hdr{};
        std::span<const double> values;

        if (protocol::decode_frame(frame_data.data(), frame_data.size(), hdr, values)) {
            for (uint32_t i = 0; i < hdr.count; ++i) {
                auto it = signal_buffers_.find(i);
                if (it != signal_buffers_.end()) {
                    it->second.push(hdr.time, values[i]);
                }
            }
        }
    }
}

void App::render_connection_status() {
    auto state = client_->state();
    ImVec4 color;
    const char *label = nullptr;

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
}

void App::render_signal_tree() {
    if (!schema_received_) {
        ImGui::TextDisabled("Waiting for schema...");
        return;
    }

    // Search filter
    static char filter[128] = "";
    ImGui::InputTextWithHint("##filter", "Filter signals...", filter, sizeof(filter));
    ImGui::Separator();

    auto &root = signal_tree_.root();
    for (auto &child : root.children) {
        render_signal_tree_node(*child);
    }
}

void App::render_signal_tree_node(const data::SignalTreeNode &node) {
    ImGui::PushID(node.full_path.c_str());

    if (node.is_leaf) {
        // Leaf node: show signal name + current value
        ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                        ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::TreeNodeEx(node.name.c_str(), leaf_flags);

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
        bool open = ImGui::TreeNodeEx(node.name.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        if (open) {
            for (auto &child : node.children) {
                render_signal_tree_node(*child);
            }
            ImGui::TreePop();
        }
    }

    ImGui::PopID();
}

} // namespace daedalus
