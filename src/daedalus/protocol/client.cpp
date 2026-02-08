#include "daedalus/protocol/client.hpp"

#include <cstdint>

namespace daedalus::protocol {

HermesClient::HermesClient(const std::string &url) : url_(url) {
    ws_.setUrl(url_);

    // Auto-reconnect with exponential backoff: 1s initial, 30s max
    ws_.enableAutomaticReconnection();
    ws_.setMinWaitBetweenReconnectionRetries(1000);
    ws_.setMaxWaitBetweenReconnectionRetries(30000);

    ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr &msg) { on_message(msg); });
}

HermesClient::~HermesClient() { disconnect(); }

void HermesClient::connect() {
    state_.store(ConnectionState::Connecting, std::memory_order_relaxed);
    ws_.start();
}

void HermesClient::disconnect() {
    ws_.stop();
    state_.store(ConnectionState::Disconnected, std::memory_order_relaxed);
}

void HermesClient::subscribe(const std::vector<std::string> &patterns) {
    nlohmann::json signals_arr = nlohmann::json::array();
    for (auto &p : patterns) {
        signals_arr.push_back(p);
    }
    send_command("subscribe", {{"signals", signals_arr}});
}

void HermesClient::pause() { send_command("pause"); }
void HermesClient::resume() { send_command("resume"); }
void HermesClient::reset() { send_command("reset"); }

void HermesClient::step(int count) { send_command("step", {{"count", count}}); }

void HermesClient::set_signal(const std::string &signal, double value) {
    send_command("set", {{"signal", signal}, {"value", value}});
}

void HermesClient::send_command(const std::string &action, const nlohmann::json &params) {
    auto cmd = format_command(action, params);
    ws_.send(cmd.dump());
}

nlohmann::json HermesClient::format_command(const std::string &action,
                                            const nlohmann::json &params) {
    nlohmann::json cmd;
    cmd["action"] = action;
    if (!params.empty()) {
        cmd["params"] = params;
    }
    return cmd;
}

void HermesClient::on_message(const ix::WebSocketMessagePtr &msg) {
    switch (msg->type) {
    case ix::WebSocketMessageType::Message:
        if (msg->binary) {
            // Binary telemetry frame — validate magic before queueing
            auto &data = msg->str;
            if (data.size() >= sizeof(TelemetryHeader)) {
                uint32_t magic = 0;
                std::memcpy(&magic, data.data(), sizeof(magic));
                if (magic == kTelemetryMagic) {
                    std::vector<uint8_t> frame(data.begin(), data.end());
                    telemetry_queue_.try_push(std::move(frame));
                }
            }
        } else {
            // Text frame (JSON) — push raw string for render thread to parse
            event_queue_.try_push(std::string(msg->str));
        }
        break;

    case ix::WebSocketMessageType::Open:
        state_.store(ConnectionState::Connected, std::memory_order_relaxed);
        event_queue_.try_push(R"({"type":"connection","event":"connected"})");
        break;

    case ix::WebSocketMessageType::Close:
        state_.store(ConnectionState::Disconnected, std::memory_order_relaxed);
        event_queue_.try_push(R"({"type":"connection","event":"disconnected"})");
        break;

    case ix::WebSocketMessageType::Error:
        state_.store(ConnectionState::Error, std::memory_order_relaxed);
        event_queue_.try_push(R"({"type":"connection","event":"error","message":")" +
                              msg->errorInfo.reason + "\"}");
        break;

    default:
        break;
    }
}

} // namespace daedalus::protocol
