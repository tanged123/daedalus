# Phase 2 Implementation Plan: Signal Plotter

> **Created**: 2026-02-12
> **Branch**: `phase2`
> **Status**: Planning
> **Depends on**: Phase 1 (complete)

---

## 1. Phase 2 Objectives

Phase 2 transforms Daedalus from a signal browser into a real-time telemetry plotting tool. The deliverables from the bootstrap guide Section 16:

| # | Deliverable | Priority |
|:--|:------------|:---------|
| 1 | ImPlot integration (provided by ImGui Bundle) | High |
| 2 | SignalBuffer enhancements for zero-copy plotting | High |
| 3 | Drag-and-drop signals from tree onto plot areas | High |
| 4 | Scrolling time-series plots with auto-scaling | High |
| 5 | Multiple Y-axes with independent scaling | Medium |
| 6 | Cursors, markers, statistics overlay (min/max/mean) | Medium |

---

## 2. Architecture Overview

### 2.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                         App (render thread)                         │
│                                                                     │
│  ┌──────────────┐   drag-drop    ┌────────────────────────────┐    │
│  │ Signal Tree  │ ──────────────→│     PlotManager            │    │
│  │ (Phase 1)    │  "SIGNAL"      │  ┌─────────────────────┐   │    │
│  │              │  payload       │  │ PlotPanel #0        │   │    │
│  └──────────────┘                │  │  signals: [0,2,4]   │   │    │
│                                  │  │  y_axis_map         │   │    │
│  ┌──────────────┐   read via     │  │  history: 10s       │   │    │
│  │SignalBuffers │←──getter fn───│  │  live_mode: true    │   │    │
│  │ map<idx,buf> │               │  └─────────────────────┘   │    │
│  │ (Phase 1)    │               │  ┌─────────────────────┐   │    │
│  └──────────────┘               │  │ PlotPanel #1        │   │    │
│                                  │  │  signals: [1,3]     │   │    │
│                                  │  └─────────────────────┘   │    │
│                                  └────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 New Files

```
include/daedalus/views/
├── plotter.hpp           # PlotPanel + PlotManager classes

src/daedalus/views/
├── plotter.cpp           # Rendering logic, drag-drop targets

tests/views/
├── test_plotter.cpp      # Unit tests for plot data structures
```

### 2.3 Modified Files

```
include/daedalus/app.hpp          # Add PlotManager member, new render methods
src/daedalus/app.cpp              # Wire drag-drop sources, register plot windows, add toolbar
include/daedalus/data/signal_buffer.hpp  # Add binary_search_time() for viewport culling
```

---

## 3. Design Decisions

### 3.1 Zero-Copy Plotting with PlotLineG

**Problem**: The current `SignalBuffer::copy_to()` copies the entire ring buffer into staging vectors each frame. With 90 signals x 18,000 samples, that is ~25 MB of copies per frame at 60 fps.

**Solution**: Use ImPlot's `PlotLineG` (getter-based) API to read directly from the ring buffer without copying:

```cpp
// Zero-copy getter — reads directly from SignalBuffer's ring buffer
static ImPlotPoint signal_getter(int idx, void* user_data) {
    auto* buf = static_cast<const data::SignalBuffer*>(user_data);
    return ImPlotPoint(buf->time_at(idx), buf->value_at(idx));
}
```

**Why this works**: `SignalBuffer::time_at()` and `value_at()` already handle the ring buffer's circular indexing via `physical_index()`. The getter simply wraps this in ImPlot's expected callback signature.

**Trade-off**: Per-point function call overhead vs. zero allocation. At 18,000 points this is <0.5ms — well within our 16ms frame budget. The win is eliminating all memory allocation in the hot path.

### 3.2 Viewport Culling for Large Histories

When the user zooms into a 2-second window of a 5-minute history, plotting all 18,000 points is wasteful (ImPlot will cull invisible points, but still iterates them).

**Solution**: Binary search on the time array to find the visible range, then pass only that subset to `PlotLineG`:

```cpp
// Find first sample index where time >= t_min
size_t find_first_from(double t_min) const;

// In render: only plot visible range
auto [vis_start, vis_count] = find_visible_range(buffer, x_min, x_max);
PlotLineG(label, getter_with_offset, &ctx, vis_count);
```

This is a Phase 2 optimization that becomes important when rendering many signals simultaneously. Simple linear scan is acceptable as a starting point; binary search can be added if profiling shows need.

### 3.3 Plot Panel as the Unit of Composition

Each **PlotPanel** is one ImPlot chart with its own:
- Set of assigned signals (drag-dropped from the tree)
- Up to 3 Y-axes (Y1, Y2, Y3) with independent scaling
- Time window configuration (history length)
- Live/paused mode toggle
- Auto-fit behavior

The **PlotManager** owns all panels. Users can create new panels, close panels, and rearrange them via HelloImGui docking. Each panel is a dockable window.

**Why panels, not a single plot?** Mission control operators typically need to see related signals grouped together (e.g., all position signals in one plot, all rates in another). Multiple panels with different Y-scales are more useful than one overcrowded plot.

### 3.4 Signal Color Assignment

ImPlot has a built-in colormap that auto-assigns distinct colors to each series within a plot. We use this by default (no manual color management needed). Users can override individual signal colors via right-click context menu (future enhancement, not in Phase 2 scope).

### 3.5 Drag-and-Drop Payload Design

The drag-drop payload carries the signal's **buffer index** (the key into `signal_buffers_`). The tree node already stores this as `signal_index`. This is a lightweight `size_t` value, making the drag-drop mechanism trivially simple.

**Payload type tags**:
- `"DND_SIGNAL"` — single leaf signal (most common)
- `"DND_GROUP"` — entire subtree/module (stretch goal, not in MVP)

### 3.6 Time Synchronization Across Panels

All plot panels share the same X-axis time range when in live mode. This is achieved by:
1. `PlotManager` maintains a single `current_time` value (updated from the latest telemetry frame)
2. Each panel in live mode uses `SetupAxisLimits(X1, current_time - history, current_time, ImPlotCond_Always)`
3. When a user pans/zooms one panel, only that panel exits live mode — others continue scrolling

This gives synchronized real-time views with per-panel override capability.

---

## 4. Implementation Steps

### Step 1: PlotPanel Data Model

**Files**: `include/daedalus/views/plotter.hpp`

**Why first**: Pure data structures with no rendering. Everything else depends on these types.

**Definitions**:

```cpp
namespace daedalus::views {

/// A signal assigned to a plot panel.
struct PlottedSignal {
    size_t buffer_index;        // Key into App::signal_buffers_
    std::string label;          // Display name (e.g., "vehicle.position.x")
    ImAxis y_axis = ImAxis_Y1;  // Which Y-axis this signal plots against
};

/// Configuration and state for a single ImPlot chart.
struct PlotPanel {
    std::string id;             // Unique panel ID for ImGui (e.g., "Plot 0")
    std::string title;          // Display title (user-editable)
    std::vector<PlottedSignal> signals;

    float history_seconds = 10.0f;  // X-axis visible window width
    bool live_mode = true;          // Auto-scroll with real-time data
    bool auto_fit_y1 = true;        // Auto-fit Y1 axis to visible data
    bool auto_fit_y2 = true;        // Auto-fit Y2 axis to visible data
    bool auto_fit_y3 = true;        // Auto-fit Y3 axis to visible data
    bool show_y2 = false;           // Enable secondary Y-axis
    bool show_y3 = false;           // Enable tertiary Y-axis

    // Cursor state
    bool show_cursor = false;
    double cursor_time = 0.0;

    // Statistics overlay
    bool show_stats = false;

    // Returns true if panel has signals on the given axis
    bool has_signals_on(ImAxis axis) const;

    // Add a signal to this panel (returns false if already present)
    bool add_signal(size_t buffer_index, const std::string& label, ImAxis y_axis = ImAxis_Y1);

    // Remove a signal by buffer index
    void remove_signal(size_t buffer_index);
};

} // namespace daedalus::views
```

**Tests** (`tests/views/test_plotter.cpp`):
- Add signal to panel, verify it exists
- Add duplicate signal, verify rejected
- Remove signal, verify removed
- `has_signals_on()` returns correct result for each axis
- Default state: live_mode=true, history=10s, auto_fit=true

**Acceptance**: Data model is correct and well-tested independent of rendering.

---

### Step 2: PlotManager + Panel Lifecycle

**Files**: `include/daedalus/views/plotter.hpp` (extend), `src/daedalus/views/plotter.cpp`

**Why second**: Manages panel creation/deletion, ID assignment, time synchronization. Must exist before rendering.

**Definitions**:

```cpp
namespace daedalus::views {

/// Manages all plot panels and coordinates their rendering.
class PlotManager {
  public:
    /// Create a new empty plot panel. Returns panel index.
    size_t create_panel(const std::string& title = "");

    /// Remove a plot panel by index.
    void remove_panel(size_t index);

    /// Access a panel by index.
    PlotPanel& panel(size_t index);
    const PlotPanel& panel(size_t index) const;

    /// Number of panels.
    size_t panel_count() const;

    /// Update the current time (call each frame from App::process_telemetry).
    void set_current_time(double time);
    double current_time() const;

    /// Render all panels. Called each frame.
    /// signal_buffers: the App's signal buffer map (read-only access).
    void render(const std::map<size_t, data::SignalBuffer>& signal_buffers);

    /// Render the plot toolbar (new panel button, global controls).
    void render_toolbar();

    /// Clear all panels (called on disconnect/reconnect).
    void clear();

  private:
    std::vector<PlotPanel> panels_;
    size_t next_panel_id_ = 0;
    double current_time_ = 0.0;

    /// Render a single panel.
    void render_panel(PlotPanel& panel,
                      const std::map<size_t, data::SignalBuffer>& signal_buffers);

    /// Render panel context menu (right-click on plot area).
    void render_panel_context_menu(PlotPanel& panel);

    /// Render statistics overlay for a panel.
    void render_statistics_overlay(const PlotPanel& panel,
                                   const std::map<size_t, data::SignalBuffer>& signal_buffers);
};

} // namespace daedalus::views
```

**Panel ID Generation**: Each panel gets a monotonically increasing ID (`"Plot 0"`, `"Plot 1"`, etc.). This ensures unique ImGui IDs even if panels are deleted and recreated.

**Tests**:
- Create panel, verify count increases
- Remove panel, verify count decreases
- Create multiple panels, verify unique IDs
- Clear removes all panels
- `set_current_time` / `current_time` round-trip

**Acceptance**: Panel lifecycle works correctly. No rendering tests (those are manual).

---

### Step 3: Core Plot Rendering (Scrolling Time-Series)

**Files**: `src/daedalus/views/plotter.cpp`

**Why third**: This is the core visual output. Depends on Steps 1-2.

**Implementation**:

```cpp
void PlotManager::render_panel(PlotPanel& panel,
                                const std::map<size_t, data::SignalBuffer>& signal_buffers) {
    // ImPlot BeginPlot with the panel's unique ID
    if (ImPlot::BeginPlot(panel.id.c_str(), ImVec2(-1, -1))) {

        // X-axis: scrolling in live mode, free in paused mode
        ImPlot::SetupAxes("Time (s)", "Y1");
        if (panel.live_mode) {
            ImPlot::SetupAxisLimits(ImAxis_X1,
                current_time_ - panel.history_seconds,
                current_time_,
                ImPlotCond_Always);
        }

        // Y1 axis: auto-fit to visible data
        if (panel.auto_fit_y1) {
            ImPlot::SetupAxis(ImAxis_Y1, nullptr,
                ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit);
        }

        // Y2 axis (optional)
        if (panel.show_y2) {
            ImPlotAxisFlags y2_flags = ImPlotAxisFlags_AuxDefault;
            if (panel.auto_fit_y2) y2_flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
            ImPlot::SetupAxis(ImAxis_Y2, "Y2", y2_flags);
        }

        // Y3 axis (optional)
        if (panel.show_y3) {
            ImPlotAxisFlags y3_flags = ImPlotAxisFlags_AuxDefault;
            if (panel.auto_fit_y3) y3_flags |= ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
            ImPlot::SetupAxis(ImAxis_Y3, "Y3", y3_flags);
        }

        // Plot each signal using zero-copy getter
        for (const auto& sig : panel.signals) {
            auto it = signal_buffers.find(sig.buffer_index);
            if (it == signal_buffers.end() || it->second.empty()) continue;

            ImPlot::SetAxes(ImAxis_X1, sig.y_axis);
            ImPlot::PlotLineG(sig.label.c_str(),
                signal_getter,
                const_cast<data::SignalBuffer*>(&it->second),
                static_cast<int>(it->second.size()));
        }

        // Drag-drop target: accept signals dropped onto this plot
        if (ImGui::BeginDragDropTarget()) {
            if (auto* payload = ImGui::AcceptDragDropPayload("DND_SIGNAL")) {
                // Payload contains PlottedSignal info
                auto* info = static_cast<const DragDropSignalPayload*>(payload->Data);
                panel.add_signal(info->buffer_index, info->label);
            }
            ImGui::EndDragDropTarget();
        }

        ImPlot::EndPlot();
    }
}
```

**Key Details**:
- `ImPlotCond_Always` on X-axis forces scrolling every frame in live mode
- `ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit` makes Y-axis fit only visible data
- `PlotLineG` with getter reads directly from ring buffer (zero-copy)
- Drag-drop target inside `BeginPlot`/`EndPlot` makes the entire plot area a drop zone

**Live Mode Toggle Logic**:
- When user pans or zooms the X-axis (detected via `ImPlot::IsPlotHovered() && ImGui::IsMouseDragging()`), exit live mode for that panel
- A "Live" button in the panel toolbar re-enables live mode
- Alternative: detect user X-axis interaction via `ImPlot::GetPlotLimits()` comparison

**Acceptance**: Signals plot as scrolling time-series lines. Data updates in real-time. Y-axis auto-fits to visible data range.

---

### Step 4: Drag-and-Drop from Signal Tree

**Files**: `src/daedalus/app.cpp` (modify `render_signal_tree_node`)

**Why fourth**: This is the primary user interaction for adding signals to plots. Depends on Step 3 (drop targets exist).

**Drag-Drop Payload Structure**:

```cpp
/// Lightweight payload for signal drag-and-drop.
struct DragDropSignalPayload {
    size_t buffer_index;
    char label[256];  // Fixed-size for ImGui payload compatibility
};
```

**Source (in signal tree leaf rendering)**:

```cpp
void App::render_signal_tree_node(const data::SignalTreeNode& node, std::string_view filter) {
    ImGui::PushID(node.full_path.c_str());

    if (node.is_leaf) {
        ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf |
                                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                        ImGuiTreeNodeFlags_SpanAvailWidth;
        ImGui::TreeNodeEx(node.name.c_str(), leaf_flags);

        // === NEW: Drag source for plotting ===
        if (node.signal_index.has_value()) {
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
                DragDropSignalPayload payload{};
                payload.buffer_index = node.signal_index.value();
                snprintf(payload.label, sizeof(payload.label), "%s", node.full_path.c_str());
                ImGui::SetDragDropPayload("DND_SIGNAL", &payload, sizeof(payload));

                // Visual preview while dragging
                ImGui::TextUnformatted(node.full_path.c_str());
                ImGui::EndDragDropSource();
            }
        }

        // Existing: show current value
        if (node.signal_index.has_value()) {
            auto it = signal_buffers_.find(node.signal_index.value());
            if (it != signal_buffers_.end() && !it->second.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%.4f", it->second.last_value());
            }
        }
    } else {
        // ... existing internal node rendering (unchanged)
    }

    ImGui::PopID();
}
```

**Auto-Create Panel on Drop**: If no panels exist when the user drops a signal, automatically create one. If panels exist, the signal is added to whichever panel receives the drop.

**Double-Click Alternative**: Double-clicking a signal in the tree adds it to the most recently active plot panel (or creates one if none exist). This provides a non-drag alternative that's faster for keyboard-centric users.

**Acceptance**: User can drag a signal from the tree and drop it onto a plot. The signal appears as a line in the plot.

---

### Step 5: Plot Toolbar + Panel Management

**Files**: `src/daedalus/views/plotter.cpp`, `src/daedalus/app.cpp`

**Why fifth**: Once basic plotting works, users need controls to manage panels and configure display.

**Global Toolbar** (rendered above the plot area):

```
[+ New Plot] [History: ──●── 10s] [Live ●]
```

- **"+ New Plot"** button: Creates a new empty PlotPanel as a dockable window
- **History slider**: Adjusts the visible time window (1s to 60s, logarithmic)
- **Live toggle**: Re-enables live scrolling for all panels

**Per-Panel Controls** (in each plot panel's title bar or context menu):

Right-click on the plot area opens a context menu:
```
┌──────────────────────────┐
│ Remove signal >          │
│   ├─ vehicle.position.x  │
│   └─ vehicle.velocity.y  │
│ ──────────────────────── │
│ Assign to Y2 axis >      │
│   ├─ vehicle.position.x  │
│   └─ vehicle.velocity.y  │
│ ──────────────────────── │
│ Show cursor              │
│ Show statistics          │
│ Auto-fit Y axes          │
│ ──────────────────────── │
│ Close panel              │
└──────────────────────────┘
```

**HelloImGui Docking Integration**: Each PlotPanel is registered as a `DockableWindow` in `MainDockSpace`. This means users can rearrange, tab, and split plot panels using HelloImGui's built-in docking.

**Implementation Notes**:
- Register new panels dynamically via `runner_params.dockingParams.dockableWindows`
- Each panel window calls `plot_manager_.render_panel(panel, signal_buffers_)`
- Panel title includes signal count: `"Plot 0 (3 signals)"`

**Acceptance**: Users can create new panels, close panels, and rearrange them via docking. Right-click context menu provides signal management.

---

### Step 6: Multiple Y-Axes

**Files**: `src/daedalus/views/plotter.cpp` (enhance `render_panel`)

**Why sixth**: Needed when signals have different scales (e.g., altitude in km vs. temperature in C). Depends on Step 5 (context menu for axis assignment).

**Axis Assignment UI**: The per-panel context menu (Step 5) includes:
- "Assign to Y2 axis" submenu lists all signals in the panel
- Selecting a signal moves it from its current axis to Y2
- Y2/Y3 axes appear automatically when at least one signal is assigned to them
- `ImPlotAxisFlags_AuxDefault` places auxiliary axes on the right side

**Axis Label Auto-Generation**: If all signals on an axis share the same unit (from schema), use that unit as the axis label. Otherwise, use "Y1"/"Y2"/"Y3".

```cpp
// In render_panel, after SetupAxes:
std::string y1_label = derive_axis_label(panel, ImAxis_Y1, signal_buffers);
ImPlot::SetupAxes("Time (s)", y1_label.c_str());
```

**Independent Auto-Fit**: Each Y-axis auto-fits independently based on its own signals' visible data range. The `ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit` combo handles this automatically per axis.

**Acceptance**: Signals can be assigned to Y1, Y2, or Y3. Each axis scales independently. Axis labels reflect signal units when homogeneous.

---

### Step 7: Cursors and Statistics Overlay

**Files**: `src/daedalus/views/plotter.cpp` (enhance `render_panel`)

**Why seventh**: Polish features that enhance usability. Depends on Steps 3-6.

**Vertical Cursor (DragLineX)**:

```cpp
if (panel.show_cursor) {
    if (ImPlot::DragLineX(0, &panel.cursor_time, ImVec4(1, 1, 0, 1))) {
        // Cursor was moved by user
    }
    ImPlot::TagX(panel.cursor_time, ImVec4(1, 1, 0, 1), "%.3f s", panel.cursor_time);

    // Show interpolated values at cursor time for each signal
    for (const auto& sig : panel.signals) {
        auto it = signal_buffers.find(sig.buffer_index);
        if (it == signal_buffers.end() || it->second.empty()) continue;

        double value = interpolate_at_time(it->second, panel.cursor_time);
        ImPlot::Annotation(panel.cursor_time, value,
            ImPlot::GetLastItemColor(), ImVec2(10, 0), true,
            "%s: %.4f", sig.label.c_str(), value);
    }
}
```

**Statistics Overlay**: When enabled, renders a semi-transparent overlay in the top-right corner of the plot:

```
┌─────────────────────────────┐
│ vehicle.position.x          │
│   Min: -1.234  Max: 5.678   │
│   Mean: 2.345  Curr: 3.456  │
│                             │
│ vehicle.velocity.y          │
│   Min: -10.0  Max: 15.2     │
│   Mean: 3.1   Curr: 7.8     │
└─────────────────────────────┘
```

Statistics are computed over the **visible time window** only (not the entire buffer). This keeps the computation fast and the values meaningful.

**Implementation**: Use `ImPlot::GetPlotLimits()` to get the visible X range, scan only the visible samples, compute min/max/mean. Render with `ImGui::SetNextWindowPos()` anchored to the plot's top-right corner.

**Acceptance**: Draggable vertical cursor shows interpolated signal values. Statistics overlay shows min/max/mean/current for visible data.

---

### Step 8: App Integration + Docking Layout

**Files**: `include/daedalus/app.hpp`, `src/daedalus/app.cpp`

**Why eighth**: Final integration step. Wires PlotManager into the App lifecycle.

**App Changes**:

```cpp
class App {
    // ... existing members ...

    // NEW: Plot manager
    views::PlotManager plot_manager_;

    // NEW: Render methods
    void render_plot_toolbar();
    void register_plot_windows(HelloImGui::RunnerParams& params);
};
```

**Updated Docking Layout**:

```
 ___________________________________________
|              |                            |
| Signal Tree  |    Plot Panel 0           |
| (left 25%)   |    (time-series)          |
|              |                            |
|              |---------------------------|
|              |    Plot Panel 1           |
|              |    (time-series)          |
|              |                            |
-------------------------------------------
| Status Bar: Connected | ws://... | 90 signals | FPS: 60 |
```

- `SignalTreeSpace`: Left 25% (existing)
- `MainDockSpace`: Right 75% (plot panels dock here, can be split vertically/horizontally)
- Toolbar: Rendered at the top of `MainDockSpace` before plot panels

**process_telemetry Update**: After routing values to SignalBuffers, update PlotManager's current time:

```cpp
void App::process_telemetry() {
    std::vector<uint8_t> frame_data;
    std::vector<double> value_storage;
    while (client_->telemetry_queue().try_pop(frame_data)) {
        protocol::TelemetryHeader hdr{};
        std::span<const double> values;
        if (protocol::decode_frame(frame_data.data(), frame_data.size(), hdr, values,
                                   value_storage)) {
            for (uint32_t i = 0; i < hdr.count; ++i) {
                auto it = signal_buffers_.find(i);
                if (it != signal_buffers_.end()) {
                    it->second.push(hdr.time, values[i]);
                }
            }
            // NEW: update time for plot scrolling
            plot_manager_.set_current_time(hdr.time);
        }
    }
}
```

**Reconnection Handling**: On disconnect, `plot_manager_.clear()` removes all panel signal assignments. Panel structures are preserved (empty panels remain) so the layout is not lost. On re-subscribe, signals must be re-dragged. (Future: save/restore signal assignments by path name.)

**Acceptance**: Full end-to-end flow: Hermes streams telemetry, signals appear in tree, user drags signals to plots, plots scroll in real-time with auto-fitting axes.

---

### Step 9: End-to-End Verification

**No new files** — manual verification.

**Procedure**:
1. Start Hermes: `python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml`
2. Build and run: `./scripts/build.sh && ./build/daedalus`
3. Verify:
   - [ ] Window opens with signal tree (Phase 1 still works)
   - [ ] Click "+ New Plot" to create a plot panel
   - [ ] Drag `vehicle.position.x` from tree onto the plot
   - [ ] Signal appears as a scrolling line
   - [ ] Drag `vehicle.velocity.x` onto the same plot
   - [ ] Both signals plot with distinct colors
   - [ ] Y-axis auto-fits to visible data
   - [ ] Adjust history slider — visible time window changes
   - [ ] Pan/zoom the plot — live mode disengages
   - [ ] Click "Live" — plot snaps back to real-time scrolling
   - [ ] Right-click plot → assign a signal to Y2
   - [ ] Secondary Y-axis appears on the right
   - [ ] Enable cursor → draggable vertical line with value readouts
   - [ ] Enable statistics → min/max/mean overlay
   - [ ] Create second plot panel via "+ New Plot"
   - [ ] Dock second panel below the first (HelloImGui split)
   - [ ] Both panels scroll simultaneously in live mode
   - [ ] Disconnect Hermes → plots freeze, status shows disconnected
   - [ ] Reconnect Hermes → status reconnects, plots resume after re-drag
4. Test with full sim: `python -m hermes.cli.main run references/hermes/examples/icarus_rocket.yaml`
   - [ ] ~90 signals load in tree
   - [ ] Can plot 10+ signals across multiple panels without frame drops
5. Run tests: `./scripts/test.sh` — all pass (Phase 1 + Phase 2 tests)
6. Run CI: `./scripts/ci.sh` — clean build + all tests

**Acceptance**: Full Phase 2 feature set works end-to-end against live Hermes data.

---

## 5. Dependency Graph

```
Step 1: PlotPanel data model                    ── no deps ──
Step 2: PlotManager + lifecycle                 ── depends on Step 1 ──
Step 3: Core plot rendering (PlotLineG)         ── depends on Step 2 ──
Step 4: Drag-and-drop from signal tree          ── depends on Step 3 ──
Step 5: Plot toolbar + panel management         ── depends on Steps 3, 4 ──
Step 6: Multiple Y-axes                         ── depends on Step 5 ──
Step 7: Cursors + statistics overlay            ── depends on Step 3 ──
Step 8: App integration + docking               ── depends on Steps 3, 4, 5 ──
Step 9: End-to-end verification                 ── depends on Step 8 ──
```

**Parallelizable**: Steps 6 and 7 are independent of each other (both depend on Step 3). Steps 1-4 are sequential. Step 8 integrates everything.

**Recommended Session Flow**:
1. **Session A** — Foundation (Steps 1, 2): Data model + manager
2. **Session B** — Core Rendering (Steps 3, 4): PlotLineG + drag-drop
3. **Session C** — Controls + Polish (Steps 5, 6, 7): Toolbar, multi-axis, cursors
4. **Session D** — Integration + Verification (Steps 8, 9): Wire into App, test end-to-end

---

## 6. CMake Updates

```cmake
# Add new source files to daedalus_lib
add_library(daedalus_lib STATIC
  src/daedalus/app.cpp
  src/daedalus/protocol/schema.cpp
  src/daedalus/protocol/client.cpp
  src/daedalus/data/signal_tree.cpp
  src/daedalus/views/plotter.cpp           # NEW
)

# Add new test files
add_executable(daedalus_tests
  tests/test_main.cpp
  tests/protocol/test_telemetry.cpp
  tests/protocol/test_schema.cpp
  tests/protocol/test_client.cpp
  tests/data/test_signal_buffer.cpp
  tests/data/test_signal_tree.cpp
  tests/data/test_telemetry_queue.cpp
  tests/views/test_plotter.cpp             # NEW
)
```

The ImPlot headers are already available via ImGui Bundle (`addons.withImplot = true` in `app.cpp`). The `#include <implot.h>` is provided by the `imgui_bundle::imgui_bundle` CMake target.

---

## 7. Directory Structure After Phase 2

```
include/daedalus/
├── app.hpp                       # Updated: +PlotManager member
├── protocol/
│   ├── client.hpp                # Unchanged
│   ├── schema.hpp                # Unchanged
│   └── telemetry.hpp             # Unchanged
├── data/
│   ├── signal_buffer.hpp         # Minor update: +binary_search helper (if needed)
│   ├── signal_tree.hpp           # Unchanged
│   └── telemetry_queue.hpp       # Unchanged
└── views/
    └── plotter.hpp               # NEW: PlotPanel, PlottedSignal, PlotManager

src/daedalus/
├── main.cpp                      # Unchanged
├── app.cpp                       # Updated: drag sources, plot windows, toolbar
├── protocol/
│   ├── schema.cpp                # Unchanged
│   └── client.cpp                # Unchanged
├── data/
│   └── signal_tree.cpp           # Unchanged
└── views/
    └── plotter.cpp               # NEW: Rendering, context menus, statistics

tests/
├── test_main.cpp                 # Unchanged
├── protocol/
│   ├── test_telemetry.cpp        # Unchanged
│   ├── test_schema.cpp           # Unchanged
│   └── test_client.cpp           # Unchanged
├── data/
│   ├── test_signal_buffer.cpp    # Unchanged
│   ├── test_signal_tree.cpp      # Unchanged
│   └── test_telemetry_queue.cpp  # Unchanged
└── views/
    └── test_plotter.cpp          # NEW: PlotPanel + PlotManager data model tests
```

---

## 8. Performance Budget

### Per-Frame Costs (60 Hz, 16.67ms budget)

| Component | Est. Cost | Notes |
|:----------|:----------|:------|
| Queue drain | <0.5 ms | Same as Phase 1 |
| Frame decode + buffer push | <0.5 ms | Same as Phase 1 |
| Signal tree render | <1 ms | Same as Phase 1 |
| **PlotLineG per signal** | **~0.3 ms** | 18k points, getter callback |
| **10 signals total** | **~3 ms** | 10 × PlotLineG |
| **Statistics compute** | **<0.5 ms** | Linear scan over visible range |
| ImGui/ImPlot internals | ~5-8 ms | Line rasterization, text rendering |
| **Total** | **~10-13 ms** | Well within 16.67ms budget |

### Scaling Limits

| Scenario | Signals Plotted | Points/Signal | Expected FPS |
|:---------|:----------------|:--------------|:-------------|
| Light (4 signals) | 4 | 18,000 | 60+ fps |
| Medium (20 signals) | 20 | 18,000 | 60 fps |
| Heavy (50 signals) | 50 | 18,000 | 40-60 fps |
| Extreme (100 signals) | 100 | 18,000 | 20-30 fps |

**Mitigation for heavy loads**: Viewport culling (binary search to plot only visible samples) reduces the effective point count dramatically when zoomed in. This is the primary optimization lever and should be implemented if the medium scenario shows any frame drops.

---

## 9. Testing Strategy

### What We Test (Unit Tests)

| Area | Test Count | Description |
|:-----|:-----------|:------------|
| PlotPanel data model | ~6 | Add/remove/duplicate signals, axis assignment |
| PlotManager lifecycle | ~5 | Create/remove panels, ID uniqueness, clear |
| Signal getter | ~3 | Verify getter reads correct values from ring buffer |

**Total new tests**: ~14 cases

### What We Don't Test (Manual Verification)

- ImPlot rendering output (visual correctness)
- Drag-and-drop interaction
- Docking layout behavior
- Auto-fit visual correctness
- Cursor/statistics visual appearance

### What We Verify End-to-End (Step 9)

Full interactive testing against live Hermes data with both `websocket_telemetry.yaml` (4 signals) and `icarus_rocket.yaml` (~90 signals).

---

## 10. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| ImPlot not initialized correctly | No plots render | Low | Already confirmed: `addons.withImplot = true` works. ImPlot context created by ImmApp |
| PlotLineG getter overhead at scale | FPS drops | Medium | Viewport culling (binary search for visible range). Fallback: copy_to + PlotLine with offset |
| Drag-drop doesn't work inside ImPlot | Core feature broken | Low | ImGui's drag-drop is independent of ImPlot. Drop target on the entire BeginPlot/EndPlot region |
| HelloImGui dynamic docking window registration | Panel creation fails | Medium | Test early. If dynamic registration is limited, use a fixed pool of dockable windows |
| 32-bit vertex indices not enabled | Dense plots crash | Medium | Check ImGui Bundle's `ImDrawIdx` setting. If 16-bit, add compile definition in CMake |
| Multi-axis labels overlap | Visual clutter | Low | Use `ImPlotAxisFlags_AuxDefault` which positions Y2/Y3 on the right side |
| Statistics computation on large buffers | FPS drops | Low | Only compute over visible range. Linear scan of <1000 visible samples is negligible |

---

## 11. Phase 2 Definition of Done

- [ ] `PlotPanel` and `PlotManager` data model implemented with tests
- [ ] Zero-copy `PlotLineG` rendering with signal getter
- [ ] Drag-and-drop from signal tree to plot panels
- [ ] Scrolling time-series with `ImPlotCond_Always` X-axis
- [ ] Y-axis auto-fit with `AutoFit | RangeFit`
- [ ] Multiple Y-axes (Y1, Y2, Y3) with independent scaling
- [ ] Draggable vertical cursor with value readouts
- [ ] Statistics overlay (min/max/mean/current)
- [ ] Panel creation/deletion via toolbar
- [ ] Panel context menu (remove signal, assign axis, toggle features)
- [ ] Live/paused mode toggle
- [ ] History duration slider
- [ ] Dockable plot panels (rearrangeable via HelloImGui)
- [ ] All Phase 1 tests still pass (no regressions)
- [ ] ~14 new unit tests for Phase 2 data model
- [ ] `./scripts/ci.sh` passes (clean build + all tests)
- [ ] End-to-end verification against live Hermes data
- [ ] Works with both `websocket_telemetry.yaml` (4 signals) and `icarus_rocket.yaml` (~90 signals)
