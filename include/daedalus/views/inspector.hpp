#pragma once

#include "daedalus/data/signal_buffer.hpp"

#include <imgui.h>

#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace daedalus::views {

/// Sort column for the inspector table.
enum class InspectorSortColumn {
    Signal,
    Value,
    Unit,
};

/// Renders a searchable table of current signal values.
class SignalInspector {
  public:
    using IsWritableCallback = std::function<bool(const std::string &)>;
    using SetSignalCallback = std::function<void(const std::string &, double)>;

    void render(const std::vector<std::string> &subscribed_signals,
                const std::map<size_t, data::SignalBuffer> &signal_buffers,
                const std::unordered_map<std::string, std::string> &signal_units);
    void set_is_writable_callback(IsWritableCallback callback);
    void set_set_signal_callback(SetSignalCallback callback);

    void reset();

    // Exposed for testing the data-model logic without an ImGui context.
    void set_sort(InspectorSortColumn sort_column, bool ascending);
    void set_filter_text(const std::string &text);
    [[nodiscard]] std::vector<size_t>
    build_visible_indices(const std::vector<std::string> &subscribed_signals,
                          const std::map<size_t, data::SignalBuffer> &signal_buffers,
                          const std::unordered_map<std::string, std::string> &signal_units);

  private:
    void rebuild_sorted_indices(const std::vector<std::string> &subscribed_signals,
                                const std::map<size_t, data::SignalBuffer> &signal_buffers,
                                const std::unordered_map<std::string, std::string> &signal_units);
    [[nodiscard]] bool passes_filter(const std::string &signal_path) const;

    ImGuiTextFilter text_filter_;
    InspectorSortColumn sort_column_ = InspectorSortColumn::Signal;
    bool sort_ascending_ = true;
    IsWritableCallback is_writable_callback_;
    SetSignalCallback set_signal_callback_;

    std::vector<size_t> sorted_indices_;
    std::unordered_map<std::string, double> writable_value_cache_;
};

} // namespace daedalus::views
