#include "daedalus/views/console.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <sstream>
#include <vector>

namespace daedalus::views {

namespace {

nlohmann::json get_params(const nlohmann::json &msg) {
    if (msg.contains("params") && msg["params"].is_object()) {
        return msg["params"];
    }
    return nlohmann::json::object();
}

template <typename T>
T value_with_params_fallback(const nlohmann::json &msg, const std::string &key, const T &fallback) {
    if (msg.contains(key)) {
        return msg.value(key, fallback);
    }
    const auto params = get_params(msg);
    if (params.contains(key)) {
        return params.value(key, fallback);
    }
    return fallback;
}

} // namespace

ConsoleLog::ConsoleLog(size_t max_entries)
    : max_entries_(std::max<size_t>(max_entries, 1)),
      start_time_(std::chrono::steady_clock::now()) {}

void ConsoleLog::add(ConsoleEntryType type, const std::string &message, const std::string &detail,
                     double sim_time) {
    if (entries_.size() >= max_entries_) {
        entries_.pop_front();
    }

    const double wall = elapsed();
    entries_.push_back(ConsoleEntry{
        .type = type,
        .message = message,
        .detail = detail,
        .wall_time = wall,
        .sim_time = sim_time,
    });

    // Mirror to terminal
    const char *prefix = "SYS";
    switch (type) {
    case ConsoleEntryType::Event:
        prefix = "EVT";
        break;
    case ConsoleEntryType::Ack:
        prefix = "ACK";
        break;
    case ConsoleEntryType::Error:
        prefix = "ERR";
        break;
    case ConsoleEntryType::Command:
        prefix = "CMD";
        break;
    case ConsoleEntryType::System:
        prefix = "SYS";
        break;
    }
    std::printf("[%s] %s\n", prefix, message.c_str());
}

void ConsoleLog::add_from_json(const std::string &json_str, double sim_time) {
    auto msg = nlohmann::json::parse(json_str, nullptr, false);
    if (msg.is_discarded()) {
        add(ConsoleEntryType::System, "Malformed JSON message", json_str, sim_time);
        return;
    }

    const std::string type = msg.value("type", "");
    if (type == "event") {
        add(ConsoleEntryType::Event, format_event(msg), json_str, sim_time);
    } else if (type == "ack") {
        add(ConsoleEntryType::Ack, format_ack(msg), json_str, sim_time);
    } else if (type == "error") {
        add(ConsoleEntryType::Error, format_error(msg), json_str, sim_time);
    } else if (type == "connection") {
        add(ConsoleEntryType::System, format_connection(msg), json_str, sim_time);
    } else {
        const std::string fallback = type.empty() ? "message" : type;
        add(ConsoleEntryType::System, fallback, json_str, sim_time);
    }
}

void ConsoleLog::add_command(const std::string &action, const nlohmann::json &params) {
    nlohmann::json cmd;
    cmd["action"] = action;
    if (!params.empty()) {
        cmd["params"] = params;
    }

    add(ConsoleEntryType::Command, format_command_message(action, params), cmd.dump(), -1.0);
}

const std::deque<ConsoleEntry> &ConsoleLog::entries() const { return entries_; }

size_t ConsoleLog::size() const { return entries_.size(); }

bool ConsoleLog::empty() const { return entries_.empty(); }

void ConsoleLog::clear() { entries_.clear(); }

double ConsoleLog::elapsed() const {
    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time_);
    return delta.count();
}

std::string ConsoleLog::format_event(const nlohmann::json &msg) {
    const std::string event = msg.value("event", "");
    if (!event.empty()) {
        return event;
    }

    const std::string message = msg.value("message", "");
    if (!message.empty()) {
        return message;
    }

    return "event";
}

std::string ConsoleLog::format_ack(const nlohmann::json &msg) {
    const std::string action = msg.value("action", "");
    if (action.empty()) {
        return "ack";
    }

    if (action == "subscribe") {
        const auto count = value_with_params_fallback<uint32_t>(msg, "count", 0);
        return "subscribe (" + std::to_string(count) + " signals)";
    }

    if (action == "step") {
        std::ostringstream oss;
        const auto count = value_with_params_fallback<int>(msg, "count", 1);
        oss << "step x" << count;
        if (msg.contains("frame") || get_params(msg).contains("frame")) {
            oss << " -> frame " << value_with_params_fallback<uint64_t>(msg, "frame", 0);
        }
        return oss.str();
    }

    if (action == "set") {
        const std::string signal = value_with_params_fallback<std::string>(msg, "signal", "");
        const double value = value_with_params_fallback<double>(msg, "value", 0.0);
        if (signal.empty()) {
            return "set";
        }
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "set %s = %.6g", signal.c_str(), value);
        return buffer;
    }

    return action;
}

std::string ConsoleLog::format_error(const nlohmann::json &msg) {
    const std::string message = msg.value("message", "");
    if (!message.empty()) {
        return message;
    }
    return "error";
}

std::string ConsoleLog::format_connection(const nlohmann::json &msg) {
    const std::string event = msg.value("event", "");
    if (event == "connected") {
        return "Connected";
    }
    if (event == "disconnected") {
        return "Disconnected";
    }
    if (event == "error") {
        const std::string message = msg.value("message", "");
        if (message.empty()) {
            return "Connection error";
        }
        return "Connection error: " + message;
    }
    if (!event.empty()) {
        return "Connection " + event;
    }
    return "Connection event";
}

std::string ConsoleLog::format_command_message(const std::string &action,
                                               const nlohmann::json &params) {
    if (action == "step") {
        const int count = params.value("count", 1);
        return "step x" + std::to_string(count);
    }
    if (action == "set") {
        const std::string signal = params.value("signal", "");
        const double value = params.value("value", 0.0);
        if (signal.empty()) {
            return "set";
        }
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "set %s = %.6g", signal.c_str(), value);
        return buffer;
    }
    if (action == "subscribe") {
        if (params.contains("signals") && params["signals"].is_array()) {
            return "subscribe (" + std::to_string(params["signals"].size()) + " signals)";
        }
        return "subscribe";
    }
    return action;
}

void ConsoleView::set_replay_callback(ReplayCallback callback) {
    replay_callback_ = std::move(callback);
}

void ConsoleView::render(ConsoleLog &log) {
    render_filter_bar(log);
    ImGui::Separator();

    std::vector<const ConsoleEntry *> visible_entries;
    visible_entries.reserve(log.size());
    for (const auto &entry : log.entries()) {
        if (passes_filter(entry)) {
            visible_entries.push_back(&entry);
        }
    }

    ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_entries.size()));
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            render_entry(*visible_entries[static_cast<size_t>(i)], static_cast<size_t>(i));
        }
    }

    if (auto_scroll_ && log.size() > last_entry_count_) {
        ImGui::SetScrollHereY(1.0f);
    }
    last_entry_count_ = log.size();

    ImGui::EndChild();
}

void ConsoleView::reset() {
    show_events_ = true;
    show_acks_ = true;
    show_errors_ = true;
    show_commands_ = true;
    show_system_ = true;
    text_filter_.Clear();
    auto_scroll_ = true;
    last_entry_count_ = 0;
}

void ConsoleView::render_filter_bar(ConsoleLog &log) {
    ImGui::Checkbox("Events", &show_events_);
    ImGui::SameLine();
    ImGui::Checkbox("Acks", &show_acks_);
    ImGui::SameLine();
    ImGui::Checkbox("Errors", &show_errors_);
    ImGui::SameLine();
    ImGui::Checkbox("Commands", &show_commands_);
    ImGui::SameLine();
    ImGui::Checkbox("System", &show_system_);
    ImGui::SameLine();
    text_filter_.Draw("Filter##console", 220.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        log.clear();
        last_entry_count_ = 0;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &auto_scroll_);
}

void ConsoleView::render_entry(const ConsoleEntry &entry, size_t row_index) {
    ImGui::PushID(static_cast<int>(row_index));
    ImGui::TextDisabled("%7.2f", entry.wall_time);
    ImGui::SameLine();
    ImGui::TextColored(entry_color(entry.type), "%s", entry_prefix(entry.type));
    ImGui::SameLine();
    ImGui::TextUnformatted(entry.message.c_str());

    const bool hovered = ImGui::IsItemHovered();
    if (hovered && !entry.detail.empty()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(entry.detail.c_str());
        ImGui::EndTooltip();
    }

    if (entry.type == ConsoleEntryType::Command &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup("command_context");
    }

    if (entry.type == ConsoleEntryType::Command && ImGui::BeginPopup("command_context")) {
        if (ImGui::MenuItem("Replay command") && replay_callback_) {
            auto cmd = nlohmann::json::parse(entry.detail, nullptr, false);
            if (!cmd.is_discarded()) {
                replay_callback_(cmd.value("action", ""), cmd.value("params", nlohmann::json{}));
            }
        }

        if (ImGui::MenuItem("Copy to clipboard")) {
            const std::string line = std::string(entry_prefix(entry.type)) + " " + entry.message;
            ImGui::SetClipboardText(line.c_str());
        }

        if (ImGui::MenuItem("Copy JSON") && !entry.detail.empty()) {
            ImGui::SetClipboardText(entry.detail.c_str());
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

bool ConsoleView::passes_filter(const ConsoleEntry &entry) const {
    const bool type_enabled = (entry.type == ConsoleEntryType::Event && show_events_) ||
                              (entry.type == ConsoleEntryType::Ack && show_acks_) ||
                              (entry.type == ConsoleEntryType::Error && show_errors_) ||
                              (entry.type == ConsoleEntryType::Command && show_commands_) ||
                              (entry.type == ConsoleEntryType::System && show_system_);

    if (!type_enabled) {
        return false;
    }

    if (text_filter_.IsActive()) {
        if (text_filter_.PassFilter(entry.message.c_str())) {
            return true;
        }
        if (!entry.detail.empty() && text_filter_.PassFilter(entry.detail.c_str())) {
            return true;
        }
        return false;
    }

    return true;
}

ImVec4 ConsoleView::entry_color(ConsoleEntryType type) {
    switch (type) {
    case ConsoleEntryType::Event:
        return ImVec4(0.35f, 0.85f, 1.0f, 1.0f);
    case ConsoleEntryType::Ack:
        return ImVec4(0.40f, 1.0f, 0.40f, 1.0f);
    case ConsoleEntryType::Error:
        return ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
    case ConsoleEntryType::Command:
        return ImVec4(1.0f, 0.90f, 0.35f, 1.0f);
    case ConsoleEntryType::System:
    default:
        return ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
    }
}

const char *ConsoleView::entry_prefix(ConsoleEntryType type) {
    switch (type) {
    case ConsoleEntryType::Event:
        return "[EVT]";
    case ConsoleEntryType::Ack:
        return "[ACK]";
    case ConsoleEntryType::Error:
        return "[ERR]";
    case ConsoleEntryType::Command:
        return "[CMD]";
    case ConsoleEntryType::System:
    default:
        return "[SYS]";
    }
}

} // namespace daedalus::views
