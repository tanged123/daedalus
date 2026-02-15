#pragma once

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>

namespace daedalus::views {

/// Types of console entries for filtering and coloring.
enum class ConsoleEntryType {
    Event,
    Ack,
    Error,
    Command,
    System,
};

/// A single entry in the console log.
struct ConsoleEntry {
    ConsoleEntryType type = ConsoleEntryType::System;
    std::string message;
    std::string detail;
    double wall_time = 0.0;
    double sim_time = -1.0;
};

/// Rolling log of console entries.
class ConsoleLog {
  public:
    static constexpr size_t kDefaultMaxEntries = 1000;

    explicit ConsoleLog(size_t max_entries = kDefaultMaxEntries);

    void add(ConsoleEntryType type, const std::string &message, const std::string &detail = "",
             double sim_time = -1.0);
    void add_from_json(const std::string &json_str, double sim_time = -1.0);
    void add_command(const std::string &action, const nlohmann::json &params = {});

    [[nodiscard]] const std::deque<ConsoleEntry> &entries() const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] bool empty() const;

    void clear();
    [[nodiscard]] double elapsed() const;

  private:
    static std::string format_event(const nlohmann::json &msg);
    static std::string format_ack(const nlohmann::json &msg);
    static std::string format_error(const nlohmann::json &msg);
    static std::string format_connection(const nlohmann::json &msg);
    static std::string format_command_message(const std::string &action,
                                              const nlohmann::json &params);

    size_t max_entries_ = kDefaultMaxEntries;
    std::deque<ConsoleEntry> entries_;
    std::chrono::steady_clock::time_point start_time_;
};

/// Renders a scrollable console log with filtering and replay actions.
class ConsoleView {
  public:
    using ReplayCallback =
        std::function<void(const std::string &action, const nlohmann::json &params)>;

    void set_replay_callback(ReplayCallback callback);
    void render(ConsoleLog &log);
    void reset();

  private:
    void render_filter_bar(ConsoleLog &log);
    void render_entry(const ConsoleEntry &entry, size_t row_index);
    [[nodiscard]] bool passes_filter(const ConsoleEntry &entry) const;
    static ImVec4 entry_color(ConsoleEntryType type);
    static const char *entry_prefix(ConsoleEntryType type);

    ReplayCallback replay_callback_;
    bool show_events_ = true;
    bool show_acks_ = true;
    bool show_errors_ = true;
    bool show_commands_ = true;
    bool show_system_ = true;
    ImGuiTextFilter text_filter_;
    bool auto_scroll_ = true;
    size_t last_entry_count_ = 0;
};

} // namespace daedalus::views
