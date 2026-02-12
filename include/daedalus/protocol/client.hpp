#pragma once

#include "daedalus/data/telemetry_queue.hpp"
#include "daedalus/protocol/telemetry.hpp"

#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace daedalus::protocol {

enum class ConnectionState { Disconnected, Connecting, Connected, Error };

/// WebSocket client for the Hermes protocol.
/// Runs IXWebSocket on a background thread and pushes data to lock-free queues.
/// The render thread polls the queues each frame.
class HermesClient {
  public:
    explicit HermesClient(const std::string &url = "ws://127.0.0.1:8765");
    ~HermesClient();

    HermesClient(const HermesClient &) = delete;
    HermesClient &operator=(const HermesClient &) = delete;

    /// Start the WebSocket connection (non-blocking).
    void connect();

    /// Close the connection.
    void disconnect();

    /// Send a subscribe command.
    void subscribe(const std::vector<std::string> &patterns);

    /// Convenience control commands.
    void pause();
    void resume();
    void reset();
    void step(int count);
    void set_signal(const std::string &signal, double value);

    /// Send a generic command.
    void send_command(const std::string &action, const nlohmann::json &params = {});

    /// Access the data queues (polled by render thread).
    data::TelemetryQueue &telemetry_queue() { return telemetry_queue_; }
    data::EventQueue &event_queue() { return event_queue_; }

    /// Current connection state (atomic, safe from any thread).
    [[nodiscard]] ConnectionState state() const { return state_.load(std::memory_order_relaxed); }

    /// Format a command as JSON (for testing).
    static nlohmann::json format_command(const std::string &action,
                                         const nlohmann::json &params = {});

  private:
    void on_message(const ix::WebSocketMessagePtr &msg);

    std::string url_;
    ix::WebSocket ws_;
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    data::TelemetryQueue telemetry_queue_{512};
    data::EventQueue event_queue_{128};
};

} // namespace daedalus::protocol
