#include "daedalus/views/plotter.hpp"

#include <imgui.h>
#include <implot/implot.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace daedalus::views {

namespace {

struct GetterContext {
    const data::SignalBuffer *buffer = nullptr;
    size_t start_index = 0;
};

struct VisibleStats {
    double min_value = 0.0;
    double max_value = 0.0;
    double mean_value = 0.0;
    double current_value = 0.0;
    bool valid = false;
};

ImPlotPoint signal_getter(int idx, void *user_data) {
    auto *ctx = static_cast<GetterContext *>(user_data);
    const size_t logical_index = ctx->start_index + static_cast<size_t>(idx);
    return ImPlotPoint(ctx->buffer->time_at(logical_index), ctx->buffer->value_at(logical_index));
}

double interpolate_at_time(const data::SignalBuffer &buffer, double time) {
    if (buffer.empty()) {
        return 0.0;
    }
    if (buffer.size() == 1) {
        return buffer.value_at(0);
    }

    const size_t upper = buffer.lower_bound_time(time);
    if (upper == 0) {
        return buffer.value_at(0);
    }
    if (upper >= buffer.size()) {
        return buffer.value_at(buffer.size() - 1);
    }

    const size_t lower = upper - 1;
    const double t0 = buffer.time_at(lower);
    const double t1 = buffer.time_at(upper);
    const double v0 = buffer.value_at(lower);
    const double v1 = buffer.value_at(upper);
    if (t1 <= t0) {
        return v1;
    }
    const double alpha = (time - t0) / (t1 - t0);
    return v0 + alpha * (v1 - v0);
}

VisibleStats compute_visible_stats(const data::SignalBuffer &buffer, double x_min, double x_max) {
    VisibleStats stats{};
    if (buffer.empty()) {
        return stats;
    }

    auto [start, count] = buffer.visible_range(x_min, x_max);
    if (count == 0) {
        start = 0;
        count = buffer.size();
    }

    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double value = buffer.value_at(start + i);
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
        sum += value;
    }

    stats.min_value = min_value;
    stats.max_value = max_value;
    stats.mean_value = sum / static_cast<double>(count);
    stats.current_value = buffer.last_value();
    stats.valid = true;
    return stats;
}

ImPlotAxisFlags make_axis_flags(bool auxiliary_axis) {
    ImPlotAxisFlags flags = ImPlotAxisFlags_NoMenus;
    if (auxiliary_axis) {
        flags |= ImPlotAxisFlags_AuxDefault;
    }
    return flags;
}

std::optional<std::pair<double, double>>
compute_axis_visible_range(const PlotPanel &panel, ImAxis axis,
                           const std::map<size_t, data::SignalBuffer> &signal_buffers, double x_min,
                           double x_max) {
    bool found = false;
    double min_value = std::numeric_limits<double>::infinity();
    double max_value = -std::numeric_limits<double>::infinity();

    for (const auto &sig : panel.signals) {
        if (sig.y_axis != axis) {
            continue;
        }
        const auto it = signal_buffers.find(sig.buffer_index);
        if (it == signal_buffers.end() || it->second.empty()) {
            continue;
        }

        const auto &buffer = it->second;
        auto [start, count] = buffer.visible_range(x_min, x_max);
        if (count == 0) {
            start = 0;
            count = buffer.size();
        }
        for (size_t i = 0; i < count; ++i) {
            const double v = buffer.value_at(start + i);
            min_value = std::min(min_value, v);
            max_value = std::max(max_value, v);
            found = true;
        }
    }

    if (!found) {
        return std::nullopt;
    }
    return std::make_pair(min_value, max_value);
}

void apply_auto_fit_limits(const PlotPanel &panel, ImAxis axis,
                           const std::map<size_t, data::SignalBuffer> &signal_buffers, double x_min,
                           double x_max) {
    const auto range = compute_axis_visible_range(panel, axis, signal_buffers, x_min, x_max);
    if (!range.has_value()) {
        return;
    }

    double min_value = range->first;
    double max_value = range->second;
    const double span = max_value - min_value;
    const double fallback_span = std::max(std::abs(max_value), 1.0) * 0.02;
    const double effective_span = span > 1e-12 ? span : fallback_span;
    const double pad = effective_span * static_cast<double>(panel.y_padding_percent / 100.0f);

    min_value -= pad;
    max_value += pad;
    if (min_value >= max_value) {
        min_value -= 0.5;
        max_value += 0.5;
    }

    ImPlot::SetupAxisLimits(axis, min_value, max_value, ImPlotCond_Always);
}

} // namespace

bool PlotPanel::has_signals_on(ImAxis axis) const {
    return std::any_of(signals.begin(), signals.end(),
                       [axis](const PlottedSignal &sig) { return sig.y_axis == axis; });
}

bool PlotPanel::add_signal(size_t buffer_index, const std::string &label, ImAxis y_axis) {
    const auto exists =
        std::any_of(signals.begin(), signals.end(),
                    [buffer_index](const auto &sig) { return sig.buffer_index == buffer_index; });
    if (exists) {
        return false;
    }
    signals.push_back(PlottedSignal{buffer_index, label, y_axis});
    return true;
}

void PlotPanel::remove_signal(size_t buffer_index) {
    signals.erase(std::remove_if(
                      signals.begin(), signals.end(),
                      [buffer_index](const auto &sig) { return sig.buffer_index == buffer_index; }),
                  signals.end());
}

bool PlotPanel::set_signal_axis(size_t buffer_index, ImAxis axis) {
    for (auto &sig : signals) {
        if (sig.buffer_index == buffer_index) {
            sig.y_axis = axis;
            return true;
        }
    }
    return false;
}

size_t PlotManager::create_panel(const std::string &title) {
    PlotPanel panel;
    panel.id = "Plot " + std::to_string(next_panel_id_++);
    panel.title = title.empty() ? panel.id : title;
    panel.history_seconds = global_history_seconds_;
    panel.cursor_time = current_time_;
    panels_.push_back(panel);
    active_panel_index_ = panels_.size() - 1;
    return panels_.size() - 1;
}

void PlotManager::remove_panel(size_t index) {
    if (index >= panels_.size()) {
        return;
    }
    panels_.erase(panels_.begin() + static_cast<std::ptrdiff_t>(index));
    if (!active_panel_index_.has_value()) {
        return;
    }
    if (*active_panel_index_ == index) {
        active_panel_index_.reset();
    } else if (*active_panel_index_ > index) {
        active_panel_index_ = *active_panel_index_ - 1;
    }
}

PlotPanel &PlotManager::panel(size_t index) { return panels_.at(index); }

const PlotPanel &PlotManager::panel(size_t index) const { return panels_.at(index); }

size_t PlotManager::panel_count() const { return panels_.size(); }

void PlotManager::set_current_time(double time) { current_time_ = time; }

double PlotManager::current_time() const { return current_time_; }

void PlotManager::render(const std::map<size_t, data::SignalBuffer> &signal_buffers) {
    if (panels_.empty()) {
        return;
    }

    const int column_count =
        std::clamp(grid_columns_, 1, std::max(1, static_cast<int>(panels_.size())));
    const ImGuiTableFlags table_flags = ImGuiTableFlags_SizingStretchSame |
                                        ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    std::vector<size_t> panels_to_remove;

    if (ImGui::BeginTable("plot_grid", column_count, table_flags)) {
        for (size_t i = 0; i < panels_.size(); ++i) {
            if (i % static_cast<size_t>(column_count) == 0) {
                ImGui::TableNextRow();
            }
            ImGui::TableSetColumnIndex(static_cast<int>(i % static_cast<size_t>(column_count)));

            bool request_close = false;
            render_panel(i, panels_[i], signal_buffers, request_close);
            if (request_close) {
                panels_to_remove.push_back(i);
            }
        }

        ImGui::EndTable();
    }

    for (auto it = panels_to_remove.rbegin(); it != panels_to_remove.rend(); ++it) {
        remove_panel(*it);
    }
}

void PlotManager::render_toolbar() {
    ImGui::SeparatorText("Plot Controls");
    if (ImGui::Button("+ New Plot")) {
        create_panel();
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::SliderFloat("History (s)", &global_history_seconds_, 1.0f, 60.0f, "%.1f",
                           ImGuiSliderFlags_Logarithmic)) {
        for (auto &panel : panels_) {
            panel.history_seconds = global_history_seconds_;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Live All")) {
        set_all_live();
    }

    ImGui::SameLine();
    ImGui::TextDisabled("%zu panels", panels_.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("Columns", &grid_columns_, 1, 4);

    if (panels_.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Drop a signal here to create the first plot panel.");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kDndSignalPayloadType)) {
                if (payload->DataSize == static_cast<int>(sizeof(DragDropSignalPayload))) {
                    const auto *drop_data =
                        static_cast<const DragDropSignalPayload *>(payload->Data);
                    const size_t panel_index = create_panel();
                    add_signal_to_panel(panel_index, drop_data->buffer_index, drop_data->label);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::Separator();
}

void PlotManager::clear() {
    panels_.clear();
    active_panel_index_.reset();
    next_panel_id_ = 0;
}

void PlotManager::clear_panel_signals() {
    for (auto &panel : panels_) {
        panel.signals.clear();
        panel.show_y2 = false;
        panel.show_y3 = false;
    }
}

void PlotManager::set_all_live() {
    for (auto &panel : panels_) {
        panel.live_mode = true;
        panel.cursor_time = current_time_;
    }
}

void PlotManager::set_signal_unit_lookup(
    std::function<std::optional<std::string>(const std::string &)> lookup) {
    signal_unit_lookup_ = std::move(lookup);
}

bool PlotManager::add_signal_to_panel(size_t panel_index, size_t buffer_index,
                                      const std::string &label, ImAxis y_axis) {
    if (panel_index >= panels_.size()) {
        return false;
    }

    PlotPanel &target = panels_[panel_index];
    const bool added = target.add_signal(buffer_index, label, y_axis);
    if (!added) {
        return false;
    }

    if (y_axis == ImAxis_Y2) {
        target.show_y2 = true;
    } else if (y_axis == ImAxis_Y3) {
        target.show_y3 = true;
    }
    active_panel_index_ = panel_index;
    return true;
}

bool PlotManager::add_signal_to_active_or_new_panel(size_t buffer_index, const std::string &label,
                                                    ImAxis y_axis) {
    if (!active_panel_index_.has_value() || *active_panel_index_ >= panels_.size()) {
        create_panel();
    }
    return add_signal_to_panel(*active_panel_index_, buffer_index, label, y_axis);
}

std::optional<size_t> PlotManager::active_panel_index() const { return active_panel_index_; }

std::string PlotManager::derive_axis_label(const PlotPanel &panel, ImAxis axis,
                                           const char *fallback) const {
    if (!signal_unit_lookup_) {
        return fallback;
    }

    std::optional<std::string> shared_unit;
    for (const auto &sig : panel.signals) {
        if (sig.y_axis != axis) {
            continue;
        }
        const auto unit = signal_unit_lookup_(sig.label);
        if (!unit.has_value() || unit->empty()) {
            return fallback;
        }
        if (!shared_unit.has_value()) {
            shared_unit = unit.value();
            continue;
        }
        if (shared_unit.value() != unit.value()) {
            return fallback;
        }
    }

    if (!shared_unit.has_value()) {
        return fallback;
    }
    return shared_unit.value();
}

void PlotManager::render_panel(size_t index, PlotPanel &panel,
                               const std::map<size_t, data::SignalBuffer> &signal_buffers,
                               bool &request_close) {
    ImGui::PushID(panel.id.c_str());
    ImGui::Text("%s (%zu signals)", panel.title.c_str(), panel.signals.size());
    if (ImGui::IsItemClicked()) {
        active_panel_index_ = index;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Close")) {
        request_close = true;
        ImGui::PopID();
        return;
    }

    ImGui::Checkbox("Live", &panel.live_mode);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    ImGui::SliderFloat("Window (s)", &panel.history_seconds, 1.0f, 60.0f, "%.1f",
                       ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::SliderFloat("Y pad %", &panel.y_padding_percent, 0.0f, 30.0f, "%.1f");

    if (panel.signals.empty()) {
        ImGui::TextDisabled("Drag signals from the tree and drop them here.");
    }

    ImVec2 plot_pos{0.0f, 0.0f};
    ImVec2 plot_size{0.0f, 0.0f};
    double x_min = current_time_ - static_cast<double>(panel.history_seconds);
    double x_max = current_time_;
    std::unordered_map<size_t, ImVec4> signal_colors;

    if (ImPlot::BeginPlot("##plot", ImVec2(-1.0f, panel.plot_height))) {
        panel.show_y2 = panel.show_y2 || panel.has_signals_on(ImAxis_Y2);
        panel.show_y3 = panel.show_y3 || panel.has_signals_on(ImAxis_Y3);
        const std::string y1_label = derive_axis_label(panel, ImAxis_Y1, "Y1");
        const std::string y2_label = derive_axis_label(panel, ImAxis_Y2, "Y2");
        const std::string y3_label = derive_axis_label(panel, ImAxis_Y3, "Y3");

        ImPlot::SetupAxis(ImAxis_X1, "Time (s)", ImPlotAxisFlags_NoMenus);
        ImPlot::SetupAxis(ImAxis_Y1, y1_label.c_str(), make_axis_flags(false));
        if (panel.show_y2) {
            ImPlot::SetupAxis(ImAxis_Y2, y2_label.c_str(), make_axis_flags(true));
        }
        if (panel.show_y3) {
            ImPlot::SetupAxis(ImAxis_Y3, y3_label.c_str(), make_axis_flags(true));
        }

        if (panel.live_mode) {
            ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
        }
        if (panel.auto_fit_y1) {
            apply_auto_fit_limits(panel, ImAxis_Y1, signal_buffers, x_min, x_max);
        }
        if (panel.show_y2 && panel.auto_fit_y2) {
            apply_auto_fit_limits(panel, ImAxis_Y2, signal_buffers, x_min, x_max);
        }
        if (panel.show_y3 && panel.auto_fit_y3) {
            apply_auto_fit_limits(panel, ImAxis_Y3, signal_buffers, x_min, x_max);
        }

        ImPlot::SetupFinish();
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        if (!panel.live_mode) {
            x_min = limits.X.Min;
            x_max = limits.X.Max;
        }

        plot_pos = ImPlot::GetPlotPos();
        plot_size = ImPlot::GetPlotSize();

        for (const auto &sig : panel.signals) {
            const auto it = signal_buffers.find(sig.buffer_index);
            if (it == signal_buffers.end() || it->second.empty()) {
                continue;
            }

            const auto &buffer = it->second;
            auto [visible_start, visible_count] = buffer.visible_range(x_min, x_max);
            if (visible_count == 0) {
                visible_start = 0;
                visible_count = buffer.size();
            }

            GetterContext ctx{&buffer, visible_start};
            ImPlot::SetAxes(ImAxis_X1, sig.y_axis);
            ImPlot::PlotLineG(sig.label.c_str(), signal_getter, &ctx,
                              static_cast<int>(visible_count));
            signal_colors[sig.buffer_index] = ImPlot::GetLastItemColor();
        }

        if (!panel.show_cursor) {
            panel.cursor_initialized = false;
        }

        if (panel.show_cursor) {
            if (!panel.cursor_initialized) {
                panel.cursor_time = current_time_;
                panel.cursor_initialized = true;
            }
            ImPlot::DragLineX(0, &panel.cursor_time, ImVec4(1.0f, 1.0f, 0.0f, 1.0f), 1.0f);
            ImPlot::TagX(panel.cursor_time, ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%.3f s",
                         panel.cursor_time);

            for (const auto &sig : panel.signals) {
                const auto it = signal_buffers.find(sig.buffer_index);
                if (it == signal_buffers.end() || it->second.empty()) {
                    continue;
                }
                const double interpolated = interpolate_at_time(it->second, panel.cursor_time);
                const auto color_it = signal_colors.find(sig.buffer_index);
                const ImVec4 annotation_color = color_it != signal_colors.end()
                                                    ? color_it->second
                                                    : ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
                ImPlot::SetAxes(ImAxis_X1, sig.y_axis);
                ImPlot::Annotation(panel.cursor_time, interpolated, annotation_color,
                                   ImVec2(10.0f, 0.0f), true, "%s: %.4f", sig.label.c_str(),
                                   interpolated);
            }
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kDndSignalPayloadType)) {
                if (payload->DataSize == static_cast<int>(sizeof(DragDropSignalPayload))) {
                    const auto *drop_data =
                        static_cast<const DragDropSignalPayload *>(payload->Data);
                    panel.add_signal(drop_data->buffer_index, drop_data->label);
                    active_panel_index_ = index;
                }
            }
            ImGui::EndDragDropTarget();
        }

        const bool drag_drop_active = ImGui::GetDragDropPayload() != nullptr;
        if (panel.live_mode && !drag_drop_active && ImPlot::IsPlotHovered() &&
            (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::GetIO().MouseWheel != 0.0f)) {
            panel.live_mode = false;
        }

        const bool axis_interaction = ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                                      ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                                      ImGui::GetIO().MouseWheel != 0.0f;
        if (axis_interaction) {
            if (panel.auto_fit_y1 && ImPlot::IsAxisHovered(ImAxis_Y1)) {
                panel.auto_fit_y1 = false;
            }
            if (panel.show_y2 && panel.auto_fit_y2 && ImPlot::IsAxisHovered(ImAxis_Y2)) {
                panel.auto_fit_y2 = false;
            }
            if (panel.show_y3 && panel.auto_fit_y3 && ImPlot::IsAxisHovered(ImAxis_Y3)) {
                panel.auto_fit_y3 = false;
            }
        }

        render_panel_context_menu(panel, request_close);
        ImPlot::EndPlot();
    }

    if (panel.show_stats && plot_size.x > 0.0f && plot_size.y > 0.0f) {
        render_statistics_overlay(panel, plot_pos, plot_size, signal_buffers, x_min, x_max);
    }

    const float splitter_height = 8.0f;
    const float splitter_width = ImGui::GetContentRegionAvail().x;
    ImGui::InvisibleButton("##panel_height_splitter", ImVec2(splitter_width, splitter_height));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    if (ImGui::IsItemActive()) {
        panel.plot_height =
            std::clamp(panel.plot_height + ImGui::GetIO().MouseDelta.y, 140.0f, 700.0f);
    }

    ImGui::Separator();
    ImGui::PopID();
}

void PlotManager::render_panel_context_menu(PlotPanel &panel, bool &request_close) {
    if (!ImGui::BeginPopupContextItem("plot_context", ImGuiPopupFlags_MouseButtonRight)) {
        return;
    }

    size_t remove_buffer_index = std::numeric_limits<size_t>::max();
    if (ImGui::BeginMenu("Remove signal")) {
        for (const auto &sig : panel.signals) {
            if (ImGui::MenuItem(sig.label.c_str())) {
                remove_buffer_index = sig.buffer_index;
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Assign axis")) {
        for (const auto &sig : panel.signals) {
            if (ImGui::BeginMenu(sig.label.c_str())) {
                if (ImGui::MenuItem("Y1", nullptr, sig.y_axis == ImAxis_Y1)) {
                    panel.set_signal_axis(sig.buffer_index, ImAxis_Y1);
                }
                if (ImGui::MenuItem("Y2", nullptr, sig.y_axis == ImAxis_Y2)) {
                    panel.set_signal_axis(sig.buffer_index, ImAxis_Y2);
                    panel.show_y2 = true;
                }
                if (ImGui::MenuItem("Y3", nullptr, sig.y_axis == ImAxis_Y3)) {
                    panel.set_signal_axis(sig.buffer_index, ImAxis_Y3);
                    panel.show_y3 = true;
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    ImGui::MenuItem("Show cursor", nullptr, &panel.show_cursor);
    ImGui::MenuItem("Show statistics", nullptr, &panel.show_stats);
    ImGui::MenuItem("Auto-fit Y1", nullptr, &panel.auto_fit_y1);
    ImGui::MenuItem("Auto-fit Y2", nullptr, &panel.auto_fit_y2);
    ImGui::MenuItem("Auto-fit Y3", nullptr, &panel.auto_fit_y3);
    ImGui::MenuItem("Show Y2 axis", nullptr, &panel.show_y2);
    ImGui::MenuItem("Show Y3 axis", nullptr, &panel.show_y3);
    ImGui::Separator();
    if (ImGui::MenuItem("Close panel")) {
        request_close = true;
    }

    ImGui::EndPopup();

    if (remove_buffer_index != std::numeric_limits<size_t>::max()) {
        panel.remove_signal(remove_buffer_index);
    }
}

void PlotManager::render_statistics_overlay(
    const PlotPanel &panel, const ImVec2 &plot_pos, const ImVec2 &plot_size,
    const std::map<size_t, data::SignalBuffer> &signal_buffers, double x_min, double x_max) {
    ImGui::SetNextWindowPos(ImVec2(plot_pos.x + plot_size.x - 12.0f, plot_pos.y + 12.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.35f);

    const std::string overlay_name = "Stats##" + panel.id;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!ImGui::Begin(overlay_name.c_str(), nullptr, flags)) {
        ImGui::End();
        return;
    }

    for (const auto &sig : panel.signals) {
        const auto it = signal_buffers.find(sig.buffer_index);
        if (it == signal_buffers.end() || it->second.empty()) {
            continue;
        }

        const VisibleStats stats = compute_visible_stats(it->second, x_min, x_max);
        if (!stats.valid) {
            continue;
        }

        ImGui::TextUnformatted(sig.label.c_str());
        ImGui::Text("Min: %.4f  Max: %.4f", stats.min_value, stats.max_value);
        ImGui::Text("Mean: %.4f  Curr: %.4f", stats.mean_value, stats.current_value);
        ImGui::Separator();
    }

    ImGui::End();
}

} // namespace daedalus::views
