#include "daedalus/views/inspector.hpp"

#include "daedalus/views/plotter.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

namespace daedalus::views {

namespace {

double signal_value_or_nan(const std::map<size_t, data::SignalBuffer> &signal_buffers,
                           size_t index) {
    const auto it = signal_buffers.find(index);
    if (it == signal_buffers.end() || it->second.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return it->second.last_value();
}

std::string signal_unit_or_empty(const std::vector<std::string> &subscribed_signals,
                                 const std::unordered_map<std::string, std::string> &signal_units,
                                 size_t index) {
    if (index >= subscribed_signals.size()) {
        return "";
    }
    const auto unit_it = signal_units.find(subscribed_signals[index]);
    if (unit_it == signal_units.end()) {
        return "";
    }
    return unit_it->second;
}

} // namespace

void SignalInspector::render(const std::vector<std::string> &subscribed_signals,
                             const std::map<size_t, data::SignalBuffer> &signal_buffers,
                             const std::unordered_map<std::string, std::string> &signal_units) {
    if (!ImGui::BeginTable("InspectorTable", 4,
                           ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_DefaultSort);
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("Unit");
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_NoSort);
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
        if (sort_specs->Specs != nullptr && sort_specs->SpecsCount > 0 && sort_specs->SpecsDirty) {
            const ImGuiTableColumnSortSpecs &spec = sort_specs->Specs[0];
            if (spec.ColumnIndex == 0) {
                sort_column_ = InspectorSortColumn::Signal;
            } else if (spec.ColumnIndex == 1) {
                sort_column_ = InspectorSortColumn::Value;
            } else if (spec.ColumnIndex == 2) {
                sort_column_ = InspectorSortColumn::Unit;
            }
            sort_ascending_ = spec.SortDirection != ImGuiSortDirection_Descending;
            sort_specs->SpecsDirty = false;
        }
    }

    const std::vector<size_t> visible_indices =
        build_visible_indices(subscribed_signals, signal_buffers, signal_units);

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_indices.size()));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const size_t idx = visible_indices[static_cast<size_t>(row)];
            if (idx >= subscribed_signals.size()) {
                continue;
            }

            const std::string &path = subscribed_signals[idx];
            const auto buf_it = signal_buffers.find(idx);

            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(path.c_str());
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers |
                                           ImGuiDragDropFlags_SourceAllowNullID)) {
                DragDropSignalPayload payload{};
                payload.buffer_index = idx;
                std::snprintf(payload.label, sizeof(payload.label), "%s", path.c_str());
                ImGui::SetDragDropPayload(kDndSignalPayloadType, &payload, sizeof(payload));
                ImGui::TextUnformatted(path.c_str());
                ImGui::EndDragDropSource();
            }

            ImGui::TableNextColumn();
            if (buf_it != signal_buffers.end() && !buf_it->second.empty()) {
                ImGui::Text("%.6g", buf_it->second.last_value());
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            const std::string unit = signal_unit_or_empty(subscribed_signals, signal_units, idx);
            if (!unit.empty()) {
                ImGui::TextUnformatted(unit.c_str());
            } else {
                ImGui::TextDisabled("--");
            }

            ImGui::TableNextColumn();
            if (buf_it != signal_buffers.end() && !buf_it->second.empty()) {
                ImGui::Text("%.3f", buf_it->second.last_time());
            } else {
                ImGui::TextDisabled("--");
            }
        }
    }

    ImGui::EndTable();
}

void SignalInspector::reset() {
    set_filter_text("");
    sort_column_ = InspectorSortColumn::Signal;
    sort_ascending_ = true;
    last_signal_count_ = 0;
    last_sort_column_ = InspectorSortColumn::Signal;
    last_sort_ascending_ = true;
    sorted_indices_.clear();
}

void SignalInspector::set_sort(InspectorSortColumn sort_column, bool ascending) {
    sort_column_ = sort_column;
    sort_ascending_ = ascending;
}

void SignalInspector::set_filter_text(const std::string &text) {
    std::snprintf(text_filter_.InputBuf, sizeof(text_filter_.InputBuf), "%s", text.c_str());
    text_filter_.Build();
}

std::vector<size_t> SignalInspector::build_visible_indices(
    const std::vector<std::string> &subscribed_signals,
    const std::map<size_t, data::SignalBuffer> &signal_buffers,
    const std::unordered_map<std::string, std::string> &signal_units) {
    rebuild_sorted_indices(subscribed_signals, signal_buffers, signal_units);

    std::vector<size_t> visible;
    visible.reserve(sorted_indices_.size());
    for (const size_t idx : sorted_indices_) {
        if (idx >= subscribed_signals.size()) {
            continue;
        }
        if (!passes_filter(subscribed_signals[idx])) {
            continue;
        }
        visible.push_back(idx);
    }
    return visible;
}

void SignalInspector::rebuild_sorted_indices(
    const std::vector<std::string> &subscribed_signals,
    const std::map<size_t, data::SignalBuffer> &signal_buffers,
    const std::unordered_map<std::string, std::string> &signal_units) {
    if (last_signal_count_ == subscribed_signals.size() && last_sort_column_ == sort_column_ &&
        last_sort_ascending_ == sort_ascending_) {
        return;
    }

    sorted_indices_.resize(subscribed_signals.size());
    for (size_t i = 0; i < subscribed_signals.size(); ++i) {
        sorted_indices_[i] = i;
    }

    const auto compare = [&](size_t lhs, size_t rhs) {
        if (lhs >= subscribed_signals.size() || rhs >= subscribed_signals.size()) {
            return lhs < rhs;
        }

        int ordering = 0;
        if (sort_column_ == InspectorSortColumn::Signal) {
            ordering = subscribed_signals[lhs].compare(subscribed_signals[rhs]);
        } else if (sort_column_ == InspectorSortColumn::Value) {
            const double lv = signal_value_or_nan(signal_buffers, lhs);
            const double rv = signal_value_or_nan(signal_buffers, rhs);
            const bool l_missing = std::isnan(lv);
            const bool r_missing = std::isnan(rv);
            if (l_missing != r_missing) {
                ordering = l_missing ? 1 : -1;
            } else if (!l_missing && !r_missing) {
                if (lv < rv) {
                    ordering = -1;
                } else if (lv > rv) {
                    ordering = 1;
                }
            }
            if (ordering == 0) {
                ordering = subscribed_signals[lhs].compare(subscribed_signals[rhs]);
            }
        } else if (sort_column_ == InspectorSortColumn::Unit) {
            const std::string lu = signal_unit_or_empty(subscribed_signals, signal_units, lhs);
            const std::string ru = signal_unit_or_empty(subscribed_signals, signal_units, rhs);
            ordering = lu.compare(ru);
            if (ordering == 0) {
                ordering = subscribed_signals[lhs].compare(subscribed_signals[rhs]);
            }
        }

        if (sort_ascending_) {
            return ordering < 0;
        }
        return ordering > 0;
    };

    std::sort(sorted_indices_.begin(), sorted_indices_.end(), compare);

    last_signal_count_ = subscribed_signals.size();
    last_sort_column_ = sort_column_;
    last_sort_ascending_ = sort_ascending_;
}

bool SignalInspector::passes_filter(const std::string &signal_path) const {
    return text_filter_.PassFilter(signal_path.c_str());
}

} // namespace daedalus::views
