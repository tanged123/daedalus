#pragma once

#include "daedalus/data/signal_buffer.hpp"
#include "daedalus/data/signal_tree.hpp"
#include "daedalus/protocol/client.hpp"
#include "daedalus/protocol/schema.hpp"

#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace daedalus {

/// Application entry point and lifecycle management.
/// Initializes Hello ImGui, connects to Hermes, and runs the render loop.
class App {
  public:
    App();
    ~App();

    /// Run the main application loop.
    /// Returns exit code (0 = success).
    int run(int argc, char *argv[]);

  private:
    /// Called each frame by Hello ImGui to process queued data.
    void process_events();
    void process_telemetry();

    /// UI rendering functions (called each frame).
    void render_connection_status();
    void render_signal_tree();
    void render_signal_tree_node(const data::SignalTreeNode &node, std::string_view filter);

    /// Handle a parsed JSON event from the event queue.
    void handle_event(const std::string &json_str);

    // --- State ---
    std::unique_ptr<protocol::HermesClient> client_;
    data::SignalTree signal_tree_;
    std::map<size_t, data::SignalBuffer> signal_buffers_;
    protocol::Schema current_schema_;
    std::vector<std::string> subscribed_signals_;
    std::string server_url_ = "ws://127.0.0.1:8765";
    bool schema_received_ = false;
};

} // namespace daedalus
