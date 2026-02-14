#pragma once

#include "daedalus/data/signal_buffer.hpp"

#include <implot/implot.h>

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace daedalus::views {

inline constexpr char kDndSignalPayloadType[] = "DND_SIGNAL";

/// Lightweight payload for signal drag-and-drop.
struct DragDropSignalPayload {
    size_t buffer_index = 0;
    char label[256] = {};
};

/// A signal assigned to a plot panel.
struct PlottedSignal {
    size_t buffer_index = 0;
    std::string label;
    ImAxis y_axis = ImAxis_Y1;
};

/// Configuration and state for a single ImPlot chart.
struct PlotPanel {
    std::string id;
    std::string title;
    std::vector<PlottedSignal> signals;

    float history_seconds = 10.0f;
    float plot_height = 260.0f;
    float y_padding_percent = 5.0f;
    bool live_mode = true;
    bool auto_fit_y1 = true;
    bool auto_fit_y2 = true;
    bool auto_fit_y3 = true;
    bool show_y2 = false;
    bool show_y3 = false;
    bool show_cursor = false;
    bool cursor_initialized = false;
    double cursor_time = 0.0;
    bool show_stats = false;

    [[nodiscard]] bool has_signals_on(ImAxis axis) const;
    bool add_signal(size_t buffer_index, const std::string &label, ImAxis y_axis = ImAxis_Y1);
    void remove_signal(size_t buffer_index);
    bool set_signal_axis(size_t buffer_index, ImAxis axis);
};

/// Manages all plot panels and coordinates their rendering.
class PlotManager {
  public:
    size_t create_panel(const std::string &title = "");
    void remove_panel(size_t index);
    PlotPanel &panel(size_t index);
    const PlotPanel &panel(size_t index) const;
    [[nodiscard]] size_t panel_count() const;

    void set_current_time(double time);
    [[nodiscard]] double current_time() const;

    void render(const std::map<size_t, data::SignalBuffer> &signal_buffers);
    void render_toolbar();
    void clear();
    void clear_panel_signals();
    void set_all_live();
    void
    set_signal_unit_lookup(std::function<std::optional<std::string>(const std::string &)> lookup);

    bool add_signal_to_panel(size_t panel_index, size_t buffer_index, const std::string &label,
                             ImAxis y_axis = ImAxis_Y1);
    bool add_signal_to_active_or_new_panel(size_t buffer_index, const std::string &label,
                                           ImAxis y_axis = ImAxis_Y1);
    [[nodiscard]] std::optional<size_t> active_panel_index() const;

  private:
    void render_panel(size_t index, PlotPanel &panel,
                      const std::map<size_t, data::SignalBuffer> &signal_buffers,
                      bool &request_close);
    void render_panel_context_menu(PlotPanel &panel, bool &request_close);
    void render_statistics_overlay(const PlotPanel &panel, const ImVec2 &plot_pos,
                                   const ImVec2 &plot_size,
                                   const std::map<size_t, data::SignalBuffer> &signal_buffers,
                                   double x_min, double x_max);
    [[nodiscard]] std::string derive_axis_label(const PlotPanel &panel, ImAxis axis,
                                                const char *fallback) const;

    std::vector<PlotPanel> panels_;
    size_t next_panel_id_ = 0;
    double current_time_ = 0.0;
    float global_history_seconds_ = 10.0f;
    int grid_columns_ = 1;
    std::optional<size_t> active_panel_index_;
    std::function<std::optional<std::string>(const std::string &)> signal_unit_lookup_;
};

} // namespace daedalus::views
