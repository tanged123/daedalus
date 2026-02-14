#pragma once

#include "daedalus/data/signal_buffer.hpp"

#include <imgui.h>

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
    void render(const std::vector<std::string> &subscribed_signals,
                const std::map<size_t, data::SignalBuffer> &signal_buffers,
                const std::unordered_map<std::string, std::string> &signal_units);

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

    std::vector<size_t> sorted_indices_;
    size_t last_signal_count_ = 0;
    InspectorSortColumn last_sort_column_ = InspectorSortColumn::Signal;
    bool last_sort_ascending_ = true;
};

} // namespace daedalus::views
