# Phase 3 Implementation Plan: Playback Controls + Console

> **Created**: 2026-02-14
> **Branch**: `phase3`
> **Status**: Planning
> **Depends on**: Phase 2 (complete)

---

## 1. Phase 3 Objectives

Phase 3 adds simulation control and observability to Daedalus. Where Phase 2 gave us eyes (plotting), Phase 3 gives us hands (controls) and ears (event log + inspector). The deliverables from the bootstrap guide Section 16:

| # | Deliverable | Priority |
|:--|:------------|:---------|
| 1 | Pause / resume / reset / single-step buttons (send commands to Hermes) | High |
| 2 | Event stream display (acks, state changes, errors) | High |
| 3 | Command history with replay | Medium |
| 4 | Signal value inspector (current values table, searchable) | Medium |

---

## 2. Architecture Overview

### 2.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          App (render thread)                                │
│                                                                             │
│  ┌─────────────────┐          ┌────────────────────────────────────────┐   │
│  │ Signal Tree     │          │  Plots Workspace (Phase 2)             │   │
│  │ (left dock)     │          │  ┌──────────┐ ┌──────────┐            │   │
│  │                 │          │  │ PlotPanel │ │ PlotPanel │            │   │
│  │                 │          │  └──────────┘ └──────────┘            │   │
│  └─────────────────┘          └────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────┐          ┌────────────────────────────────────────┐   │
│  │ Inspector       │   NEW    │  Console                        NEW    │   │
│  │ (bottom-left)   │◄────────│  (bottom dock)                         │   │
│  │                 │          │  ┌─────────────────────────────────┐   │   │
│  │ Current values  │          │  │ Event log + command history     │   │   │
│  │ table, search   │          │  │ [EVT] paused         t=12.34   │   │   │
│  │                 │          │  │ [CMD] resume          t=12.50   │   │   │
│  │                 │          │  │ [ACK] resume          t=12.50   │   │   │
│  └─────────────────┘          │  └─────────────────────────────────┘   │   │
│                               │  Filter: [All|Events|Commands|Errors] │   │
│                               └────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ Status Bar                                                          │   │
│  │ [Connected ●] ws://127.0.0.1:8765 | 90 signals                     │   │
│  │ [▶ Play] [⏸ Pause] [■ Reset] [⏭ Step] | State: Running | F: 1234  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 New Files

```
include/daedalus/views/
├── console.hpp            # ConsoleLog + ConsoleView classes
├── controls.hpp           # PlaybackControls class + SimulationState
├── inspector.hpp          # SignalInspector class

src/daedalus/views/
├── console.cpp            # Console rendering
├── controls.cpp           # Playback controls rendering
├── inspector.cpp          # Inspector rendering

tests/views/
├── test_console.cpp       # Console data model tests
├── test_controls.cpp      # Simulation state tracking tests
├── test_inspector.cpp     # Inspector data model tests
```

### 2.3 Modified Files

```
include/daedalus/app.hpp         # Add ConsoleLog, PlaybackControls, SignalInspector members
src/daedalus/app.cpp             # Route events to console, render new views, docking layout
CMakeLists.txt                   # Add new source and test files
```

---

## 3. Design Decisions

### 3.1 Simulation State Tracking

**Problem**: Playback controls need to know the current simulation state (running/paused) to show correct button states (e.g., disable "Pause" when already paused, show "Resume" instead).

**Solution**: Track simulation state from Hermes events. The server sends `{"type": "event", "event": "paused"}` and `{"type": "event", "event": "running"}` in response to control commands.

```cpp
enum class SimulationState {
    Unknown,    // No events received yet
    Running,    // Telemetry streaming, simulation advancing
    Paused,     // Simulation halted, no new telemetry
};
```

**State Transitions**:
- `event.event == "running"` → `Running`
- `event.event == "paused"` → `Paused`
- `event.event == "reset"` → `Paused` (reset returns to paused state)
- Connection lost → `Unknown`
- Reconnect + resume → `Running`

**Why not track from commands?** Commands are fire-and-forget over WebSocket. The server might reject them (e.g., pause when already paused). Only events represent confirmed state changes.

### 3.2 Console as a Rolling Log

**Problem**: Over a long session, thousands of events accumulate. Storing them all wastes memory and makes the UI sluggish.

**Solution**: Rolling buffer with configurable max capacity (default 1000 entries). Oldest entries are discarded when full. This matches the SignalBuffer pattern from Phase 1.

```cpp
struct ConsoleEntry {
    ConsoleEntryType type;   // Event, Ack, Error, Command, System
    std::string message;     // Formatted display text
    std::string detail;      // Original JSON or extra context
    double wall_time;        // std::chrono wall clock (for display)
    double sim_time;         // Simulation time (-1 if not applicable)
};
```

**Why wall time AND sim time?** Commands and connection events happen at wall-clock time. Events and acks relate to simulation time. Showing both lets the operator correlate real time with simulation progress.

### 3.3 Command History as Part of Console

**Problem**: Command history could be a separate data structure or part of the console.

**Solution**: Commands are logged as `ConsoleEntry` with `type = Command`. The console filter can isolate just commands. Replay is triggered from the console UI (right-click a command entry → "Replay").

**Why not separate?** Commands and events are interleaved chronologically. Seeing `[CMD] pause` followed by `[EVT] paused` in the same log makes the cause-and-effect relationship clear. A separate command history would lose this temporal context.

### 3.4 Console Entry Formatting

Each entry type has a distinct prefix and color for visual scanning:

| Type | Prefix | Color | Example |
|:-----|:-------|:------|:--------|
| Event | `[EVT]` | Cyan | `[EVT] paused` |
| Ack | `[ACK]` | Green | `[ACK] subscribe (4 signals)` |
| Error | `[ERR]` | Red | `[ERR] Unknown signal: foo.bar` |
| Command | `[CMD]` | Yellow | `[CMD] pause` |
| System | `[SYS]` | Gray | `[SYS] Connected to ws://127.0.0.1:8765` |

### 3.5 Signal Inspector Design

The inspector shows a flat table of all subscribed signals with their current values. This complements the signal tree (which shows hierarchy) and the plotter (which shows history).

**Columns**:
| Column | Source | Sortable? |
|:-------|:-------|:----------|
| Signal | `subscribed_signals_[i]` | Yes |
| Value | `signal_buffers_[i].last_value()` | Yes |
| Unit | `signal_units_[path]` | Yes |
| Time | `signal_buffers_[i].last_time()` | No |

**Search**: Text filter on signal path (case-insensitive substring match). Reuses the same `ImGuiTextFilter` pattern as the signal tree.

**Why a separate view?** The signal tree shows values inline, but it's hierarchical and collapsed by default — finding a specific signal's value requires expanding multiple levels. The inspector is a flat, scannable table optimized for quick value lookup.

### 3.6 Transport Controls Placement

**Problem**: Where should pause/resume/reset/step buttons live?

**Decision**: In the **status bar**, next to the existing connection status. The status bar is always visible regardless of docking layout, making transport controls always accessible.

**Layout**:
```
[Connected ●] ws://127.0.0.1:8765 | 90 signals | [▶] [⏸] [⏹] [⏭ 1] | Running | Frame: 1234 | t=12.34s
```

- Buttons are compact icon-style (single character or small text)
- Active button is highlighted (e.g., "Pause" highlighted when running)
- Disabled when disconnected
- Step button has an input field for step count (default 1)
- Frame counter and sim time from latest telemetry header

**Why status bar, not a toolbar?** The status bar is always visible regardless of which dockable windows are shown. A separate toolbar window could be accidentally closed. The status bar approach ensures controls are never hidden.

### 3.7 Docking Layout Update

Phase 3 adds two new dockable windows to the existing layout:

```
 ___________________________________________________
|                |                                   |
| Signal Tree    |    Plots Workspace               |
| (left 20%)     |    (top-right 60%)               |
|                |                                   |
|________________|___________________________________|
|                |                                   |
| Inspector      |    Console                       |
| (bottom-left   |    (bottom-right 60%)            |
|  20%)          |                                   |
|________________|___________________________________|
| Status Bar: Connected | Controls | State | Frame   |
|___________________________________________________|
```

- The left dock is split vertically: Signal Tree (top) + Inspector (bottom)
- The main dock is split horizontally: Plots (top) + Console (bottom)
- Console takes ~30% of the right area height
- All windows are dockable/closeable/rearrangeable via HelloImGui

---

## 4. Implementation Steps

### Step 1: Console Data Model [ ]

**Files**: `include/daedalus/views/console.hpp`

**Why first**: Pure data structures with no rendering. The console log is the central data store that playback controls, event handling, and the console view all depend on.

**Definitions**:

```cpp
namespace daedalus::views {

/// Types of console entries for filtering and coloring.
enum class ConsoleEntryType {
    Event,    // Hermes events (paused, running, reset)
    Ack,      // Hermes acknowledgments (subscribe, pause, etc.)
    Error,    // Hermes errors
    Command,  // Commands sent by user to Hermes
    System    // Connection events, internal messages
};

/// A single entry in the console log.
struct ConsoleEntry {
    ConsoleEntryType type;
    std::string message;     // Formatted display text (e.g., "pause")
    std::string detail;      // Raw JSON or extra context (shown on hover/expand)
    double wall_time;        // Wall-clock seconds since log creation
    double sim_time;         // Simulation time (-1.0 if not applicable)
};

/// Rolling log of console entries.
class ConsoleLog {
  public:
    static constexpr size_t kDefaultMaxEntries = 1000;

    explicit ConsoleLog(size_t max_entries = kDefaultMaxEntries);

    /// Add an entry to the log (discards oldest if at capacity).
    void add(ConsoleEntryType type, const std::string& message,
             const std::string& detail = "", double sim_time = -1.0);

    /// Add from a parsed Hermes JSON message (auto-detects type and formats).
    void add_from_json(const std::string& json_str, double sim_time = -1.0);

    /// Add a command entry (records what was sent to Hermes).
    void add_command(const std::string& action, const nlohmann::json& params = {});

    /// Access entries.
    const std::deque<ConsoleEntry>& entries() const;
    size_t size() const;
    bool empty() const;

    /// Clear all entries.
    void clear();

    /// Get wall-clock time elapsed since log creation.
    double elapsed() const;

  private:
    size_t max_entries_;
    std::deque<ConsoleEntry> entries_;
    std::chrono::steady_clock::time_point start_time_;

    /// Format a Hermes JSON message into a human-readable string.
    static std::string format_event(const nlohmann::json& msg);
    static std::string format_ack(const nlohmann::json& msg);
    static std::string format_error(const nlohmann::json& msg);
};

} // namespace daedalus::views
```

**Key Design Notes**:
- `std::deque` for O(1) front removal when over capacity (vs. vector erase)
- `wall_time` is computed from `steady_clock` relative to log creation (avoids epoch formatting)
- `add_from_json()` auto-detects type from `msg["type"]` and formats appropriately
- `add_command()` is called from PlaybackControls when sending commands (before the command is sent, so order is correct in the log)

**Formatting Examples**:
- Event: `"paused"` → `"paused"`
- Ack subscribe: `{"action":"subscribe","count":4}` → `"subscribe (4 signals)"`
- Ack pause: `{"action":"pause"}` → `"pause"`
- Ack step: `{"action":"step","count":10,"frame":110}` → `"step ×10 → frame 110"`
- Ack set: `{"action":"set","signal":"inputs.throttle","value":1.0}` → `"set inputs.throttle = 1.0"`
- Error: `{"message":"Unknown signal"}` → `"Unknown signal"`

**Tests** (`tests/views/test_console.cpp`):
- Add entry, verify size increases
- Add entries beyond max capacity, verify oldest discarded and size capped
- `add_from_json()` correctly parses event type → Event entry
- `add_from_json()` correctly parses ack type → Ack entry
- `add_from_json()` correctly parses error type → Error entry
- `add_command()` creates Command entry with formatted message
- `format_event()` produces expected strings for known events
- `format_ack()` produces expected strings for each ack variant (subscribe, pause, step, set)
- `format_error()` extracts error message
- `clear()` empties the log
- `elapsed()` returns positive time

**Acceptance**: Console data model stores, formats, and manages entries correctly. All formatting matches expected output.

---

### Step 2: Simulation State Tracking [ ]

**Files**: `include/daedalus/views/controls.hpp`

**Why second**: Playback controls rendering depends on knowing the current sim state. This step defines the state enum and the tracking logic, both pure data with no UI.

**Definitions**:

```cpp
namespace daedalus::views {

/// Current simulation state, determined from Hermes events.
enum class SimulationState {
    Unknown,    // No state events received yet
    Running,    // Simulation advancing, telemetry streaming
    Paused      // Simulation halted
};

/// Converts SimulationState to display string.
const char* simulation_state_label(SimulationState state);

/// Playback state and controls data model.
struct PlaybackState {
    SimulationState sim_state = SimulationState::Unknown;
    uint64_t last_frame = 0;        // Latest frame number from telemetry
    double last_sim_time = 0.0;     // Latest simulation time
    int step_count = 1;             // Step count for single-step command
    bool connected = false;         // Connection state (from HermesClient)

    /// Update state from a Hermes event JSON message.
    /// Returns true if the state changed.
    bool update_from_event(const nlohmann::json& msg);

    /// Update from telemetry header (frame + time).
    void update_from_telemetry(uint64_t frame, double sim_time);

    /// Reset state (on disconnect).
    void reset();

    /// Whether playback controls should be enabled.
    bool controls_enabled() const { return connected; }

    /// Whether specific buttons should be enabled.
    bool can_pause() const { return connected && sim_state == SimulationState::Running; }
    bool can_resume() const { return connected && sim_state != SimulationState::Running; }
    bool can_reset() const { return connected; }
    bool can_step() const { return connected && sim_state != SimulationState::Running; }
};

} // namespace daedalus::views
```

**State Update Logic** (`update_from_event`):
```cpp
bool PlaybackState::update_from_event(const nlohmann::json& msg) {
    if (msg.value("type", "") != "event") return false;

    auto event = msg.value("event", "");
    SimulationState prev = sim_state;

    if (event == "running") {
        sim_state = SimulationState::Running;
    } else if (event == "paused") {
        sim_state = SimulationState::Paused;
    } else if (event == "reset") {
        sim_state = SimulationState::Paused;
        last_frame = 0;
        last_sim_time = 0.0;
    }

    return sim_state != prev;
}
```

**Tests** (`tests/views/test_controls.cpp`):
- Initial state is Unknown, frame 0, time 0.0
- `update_from_event("running")` → Running
- `update_from_event("paused")` → Paused
- `update_from_event("reset")` → Paused, frame reset to 0
- Non-event messages return false, no state change
- `reset()` returns to Unknown
- `can_pause()` true only when Running + connected
- `can_resume()` true when Paused or Unknown + connected
- `can_step()` true when not Running + connected
- All controls disabled when not connected
- `update_from_telemetry()` updates frame + time
- `simulation_state_label()` returns correct strings

**Acceptance**: Simulation state correctly tracks Hermes events. Button enable/disable predicates are correct for all state combinations.

---

### Step 3: Playback Controls Rendering [ ]

**Files**: `src/daedalus/views/controls.cpp`, `include/daedalus/views/controls.hpp` (extend)

**Why third**: With the state model from Step 2, we can now render the transport controls. This is the highest-priority user-facing feature.

**Rendering Function**:

```cpp
namespace daedalus::views {

/// Renders playback controls in the status bar.
/// Returns a PlaybackAction if a button was clicked (nullopt otherwise).
enum class PlaybackAction {
    Pause,
    Resume,
    Reset,
    Step
};

std::optional<PlaybackAction> render_playback_controls(PlaybackState& state);

} // namespace daedalus::views
```

**Status Bar Integration**:

The existing `render_connection_status()` renders connection info in the status bar. We extend this to also show transport controls:

```
[Connected ●] ws://127.0.0.1:8765 | 90 signals | [Resume] [Pause] [Reset] [Step 1] | Running | F:1234 t:12.34s
```

**Implementation Details**:

```cpp
std::optional<PlaybackAction> render_playback_controls(PlaybackState& state) {
    std::optional<PlaybackAction> action;

    // Resume button (enabled when paused/unknown)
    ImGui::BeginDisabled(!state.can_resume());
    if (ImGui::SmallButton("Resume")) {
        action = PlaybackAction::Resume;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Pause button (enabled when running)
    ImGui::BeginDisabled(!state.can_pause());
    if (ImGui::SmallButton("Pause")) {
        action = PlaybackAction::Pause;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Reset button (always enabled when connected)
    ImGui::BeginDisabled(!state.can_reset());
    if (ImGui::SmallButton("Reset")) {
        action = PlaybackAction::Reset;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    // Step button + count input (enabled when paused)
    ImGui::BeginDisabled(!state.can_step());
    ImGui::SetNextItemWidth(40.0f);
    ImGui::InputInt("##step_count", &state.step_count, 0, 0);
    state.step_count = std::clamp(state.step_count, 1, 10000);
    ImGui::SameLine();
    if (ImGui::SmallButton("Step")) {
        action = PlaybackAction::Step;
    }
    ImGui::EndDisabled();

    // State display
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    // State label with color
    ImVec4 state_color;
    switch (state.sim_state) {
        case SimulationState::Running: state_color = ImVec4(0.4f, 1.0f, 0.4f, 1.0f); break;
        case SimulationState::Paused:  state_color = ImVec4(1.0f, 0.8f, 0.2f, 1.0f); break;
        default:                       state_color = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); break;
    }
    ImGui::TextColored(state_color, "%s", simulation_state_label(state.sim_state));

    // Frame + time display
    if (state.last_frame > 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("F:%lu t:%.2fs", state.last_frame, state.last_sim_time);
    }

    return action;
}
```

**Action Dispatch** (in `App::render_connection_status()`):

```cpp
auto action = views::render_playback_controls(playback_state_);
if (action) {
    switch (*action) {
        case views::PlaybackAction::Resume:
            client_->resume();
            console_log_.add_command("resume");
            break;
        case views::PlaybackAction::Pause:
            client_->pause();
            console_log_.add_command("pause");
            break;
        case views::PlaybackAction::Reset:
            client_->reset();
            console_log_.add_command("reset");
            break;
        case views::PlaybackAction::Step:
            client_->step(playback_state_.step_count);
            console_log_.add_command("step", {{"count", playback_state_.step_count}});
            break;
    }
}
```

**Key Details**:
- `render_playback_controls()` returns an action value rather than directly calling `client_` — this keeps the view decoupled from the protocol client
- Commands are logged to the console log before being sent (so the log shows the user's intent even if the command fails)
- `ImGui::BeginDisabled()`/`EndDisabled()` grays out buttons that can't be used in the current state
- Step count is clamped to [1, 10000] to prevent accidental huge steps

**Acceptance**: Transport buttons render in the status bar. Buttons are correctly enabled/disabled based on simulation state. Clicking a button dispatches the corresponding command.

---

### Step 4: Console View Rendering [ ]

**Files**: `include/daedalus/views/console.hpp` (extend), `src/daedalus/views/console.cpp`

**Why fourth**: The console is the primary observability tool. With the data model (Step 1) and event routing (Step 2), we can now render the event stream.

**ConsoleView Class**:

```cpp
namespace daedalus::views {

/// Renders the console log as a scrollable, filterable window.
class ConsoleView {
  public:
    /// Render the console window contents.
    /// Call inside an ImGui window (the dockable window is set up by App).
    void render(const ConsoleLog& log);

    /// Reset filter and scroll state.
    void reset();

  private:
    // Filter state
    bool show_events_ = true;
    bool show_acks_ = true;
    bool show_errors_ = true;
    bool show_commands_ = true;
    bool show_system_ = true;
    ImGuiTextFilter text_filter_;

    // Scroll state
    bool auto_scroll_ = true;
    size_t last_entry_count_ = 0;  // Track if new entries were added

    /// Render the filter toolbar.
    void render_filter_bar();

    /// Render a single entry.
    void render_entry(const ConsoleEntry& entry);

    /// Get color for entry type.
    static ImVec4 entry_color(ConsoleEntryType type);

    /// Get prefix for entry type.
    static const char* entry_prefix(ConsoleEntryType type);

    /// Check if entry passes current filters.
    bool passes_filter(const ConsoleEntry& entry) const;
};

} // namespace daedalus::views
```

**Rendering Implementation**:

```cpp
void ConsoleView::render(const ConsoleLog& log) {
    // Filter toolbar at top
    render_filter_bar();

    ImGui::Separator();

    // Scrollable log region
    ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    for (const auto& entry : log.entries()) {
        if (!passes_filter(entry)) continue;
        render_entry(entry);
    }

    // Auto-scroll to bottom when new entries arrive
    if (auto_scroll_ && log.size() > last_entry_count_) {
        ImGui::SetScrollHereY(1.0f);
    }
    last_entry_count_ = log.size();

    ImGui::EndChild();
}
```

**Filter Bar**:

```
[Events ✓] [Acks ✓] [Errors ✓] [Commands ✓] [System ✓]  Filter: [________]  [Clear] [Auto-scroll ✓]
```

- Toggle buttons for each entry type (colored to match entry colors)
- Text filter for substring search across message text
- "Clear" button clears the log
- "Auto-scroll" toggle — when enabled, scrolls to newest entry each frame

**Entry Rendering**:

```
 12.34  [EVT] paused                                    (hover: show raw JSON)
 12.50  [CMD] resume
 12.51  [ACK] resume
 15.00  [ERR] Unknown signal: foo.bar
```

- Wall time displayed as seconds with 2 decimal places (relative to session start)
- Type prefix colored by type
- Hover tooltip shows `entry.detail` (raw JSON) if non-empty
- Right-click context menu on Command entries shows "Replay" option

**Right-Click Context Menu** (Command entries only):

```
┌──────────────────────┐
│ Replay command       │
│ Copy to clipboard    │
│ Copy JSON            │
└──────────────────────┘
```

- "Replay command": Emits the command action for the App to re-send
- "Copy to clipboard": Copies formatted message to system clipboard
- "Copy JSON": Copies raw JSON detail to clipboard

**Acceptance**: Console renders a scrollable, filterable event log. Each entry type is visually distinct. Auto-scroll works. Filter toggles work correctly.

---

### Step 5: Command History + Replay [ ]

**Files**: `src/daedalus/views/console.cpp` (extend), `src/daedalus/app.cpp` (extend)

**Why fifth**: Builds on the console view (Step 4) to add replay capability. The data model already stores commands (Step 1); this step adds the replay interaction.

**Replay Mechanism**:

The console view needs a way to signal that the user wants to replay a command. Since the view shouldn't hold a reference to `HermesClient`, we use a callback approach:

```cpp
/// Extended ConsoleView with replay callback.
class ConsoleView {
  public:
    /// Set callback for command replay. Called with (action, params_json).
    using ReplayCallback = std::function<void(const std::string& action,
                                               const nlohmann::json& params)>;
    void set_replay_callback(ReplayCallback cb);

    // ... rest unchanged ...

  private:
    ReplayCallback replay_callback_;
};
```

**Command Storage for Replay**:

To replay a command, we need the original action and params. We store these in `ConsoleEntry::detail` as the formatted command JSON:

```cpp
void ConsoleLog::add_command(const std::string& action, const nlohmann::json& params) {
    nlohmann::json cmd = {{"action", action}};
    if (!params.empty()) cmd["params"] = params;
    add(ConsoleEntryType::Command, format_command_message(action, params),
        cmd.dump(), -1.0);
}
```

When the user clicks "Replay", the console parses `entry.detail` back to JSON and calls the replay callback:

```cpp
void ConsoleView::handle_replay(const ConsoleEntry& entry) {
    if (!replay_callback_ || entry.detail.empty()) return;
    auto cmd = nlohmann::json::parse(entry.detail, nullptr, false);
    if (cmd.is_discarded()) return;
    replay_callback_(cmd.value("action", ""),
                     cmd.value("params", nlohmann::json{}));
}
```

**App Integration**:

```cpp
// In App constructor or initialization:
console_view_.set_replay_callback([this](const std::string& action,
                                          const nlohmann::json& params) {
    client_->send_command(action, params);
    console_log_.add_command(action, params);  // Log the replayed command too
});
```

**Acceptance**: Commands can be replayed from the console via right-click menu. Replayed commands appear as new entries in the log. The replay callback pattern keeps the view decoupled from the client.

---

### Step 6: Signal Value Inspector [ ]

**Files**: `include/daedalus/views/inspector.hpp`, `src/daedalus/views/inspector.cpp`, `tests/views/test_inspector.cpp`

**Why sixth**: Independent of the console (Steps 1-5). Can be developed in parallel with Steps 4-5.

**SignalInspector Class**:

```cpp
namespace daedalus::views {

/// Sort column for the inspector table.
enum class InspectorSortColumn {
    Signal,
    Value,
    Unit
};

/// Renders a searchable table of current signal values.
class SignalInspector {
  public:
    /// Render the inspector window contents.
    /// subscribed_signals: ordered list of signal paths (from subscribe ack)
    /// signal_buffers: per-signal rolling history (for current value)
    /// signal_units: signal path → unit string
    void render(const std::vector<std::string>& subscribed_signals,
                const std::map<size_t, data::SignalBuffer>& signal_buffers,
                const std::unordered_map<std::string, std::string>& signal_units);

    /// Reset filter and sort state.
    void reset();

  private:
    ImGuiTextFilter text_filter_;
    InspectorSortColumn sort_column_ = InspectorSortColumn::Signal;
    bool sort_ascending_ = true;

    /// Cached sorted indices (rebuilt when sort changes or signals change).
    std::vector<size_t> sorted_indices_;
    size_t last_signal_count_ = 0;
    InspectorSortColumn last_sort_column_ = InspectorSortColumn::Signal;
    bool last_sort_ascending_ = true;

    /// Rebuild sorted index if needed.
    void update_sort(const std::vector<std::string>& subscribed_signals,
                     const std::map<size_t, data::SignalBuffer>& signal_buffers,
                     const std::unordered_map<std::string, std::string>& signal_units);
};

} // namespace daedalus::views
```

**Rendering**:

```cpp
void SignalInspector::render(const std::vector<std::string>& subscribed_signals,
                              const std::map<size_t, data::SignalBuffer>& signal_buffers,
                              const std::unordered_map<std::string, std::string>& signal_units) {
    // Search filter
    text_filter_.Draw("Filter##inspector", -FLT_MIN);

    // Table
    if (ImGui::BeginTable("InspectorTable", 4,
            ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable |
            ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg)) {

        ImGui::TableSetupScrollFreeze(0, 1);  // Freeze header row
        ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_DefaultSort);
        ImGui::TableSetupColumn("Value",  ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Unit",   ImGuiTableColumnFlags_NoSort);
        ImGui::TableSetupColumn("Time",   ImGuiTableColumnFlags_NoSort);
        ImGui::TableHeadersRow();

        // Handle sort spec changes
        if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
            if (sort_specs->SpecsDirty) {
                // Update sort column/direction from specs
                sort_specs->SpecsDirty = false;
            }
        }

        update_sort(subscribed_signals, signal_buffers, signal_units);

        // Render rows with clipper for large signal counts
        ImGuiListClipper clipper;
        // ... filter and display rows using sorted_indices_ ...

        for (size_t idx : sorted_indices_) {
            if (idx >= subscribed_signals.size()) continue;
            const auto& path = subscribed_signals[idx];

            // Apply text filter
            if (!text_filter_.PassFilter(path.c_str())) continue;

            ImGui::TableNextRow();

            // Signal name
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(path.c_str());

            // Current value
            ImGui::TableNextColumn();
            auto buf_it = signal_buffers.find(idx);
            if (buf_it != signal_buffers.end() && !buf_it->second.empty()) {
                ImGui::Text("%.6g", buf_it->second.last_value());
            } else {
                ImGui::TextDisabled("--");
            }

            // Unit
            ImGui::TableNextColumn();
            auto unit_it = signal_units.find(path);
            if (unit_it != signal_units.end()) {
                ImGui::TextUnformatted(unit_it->second.c_str());
            }

            // Last update time
            ImGui::TableNextColumn();
            if (buf_it != signal_buffers.end() && !buf_it->second.empty()) {
                ImGui::Text("%.3f", buf_it->second.last_time());
            }
        }

        ImGui::EndTable();
    }
}
```

**Features**:
- **Search filter**: Case-insensitive substring match on signal path
- **Column sorting**: Click column headers to sort by signal name or value
- **Frozen header**: Header row stays visible while scrolling
- **Row backgrounds**: Alternating row colors via `ImGuiTableFlags_RowBg`
- **Value formatting**: `%.6g` for compact numeric display (avoids trailing zeros)
- **Missing data**: Shows "--" for signals with no data yet

**Drag-Drop Source** (bonus integration with Phase 2):

Each row in the inspector can also be a drag source for adding signals to plots:

```cpp
// After rendering the signal name cell:
if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
    DragDropSignalPayload payload{};
    payload.buffer_index = idx;
    snprintf(payload.label, sizeof(payload.label), "%s", path.c_str());
    ImGui::SetDragDropPayload(kDndSignalPayloadType, &payload, sizeof(payload));
    ImGui::TextUnformatted(path.c_str());
    ImGui::EndDragDropSource();
}
```

**Tests** (`tests/views/test_inspector.cpp`):
- Sort by signal name ascending/descending
- Sort by value ascending/descending
- Filter correctly hides non-matching signals
- Reset clears filter and restores default sort
- Empty signal list renders without crash
- sorted_indices_ correctly maps to original signal indices

**Acceptance**: Inspector shows a searchable, sortable table of all subscribed signal values. Values update in real-time. Signals can be dragged from the inspector to plots.

---

### Step 7: App Integration + Docking Layout [ ]

**Files**: `include/daedalus/app.hpp`, `src/daedalus/app.cpp`, `CMakeLists.txt`

**Why seventh**: Final integration step. Wires all new components into the App lifecycle and configures the docking layout.

**New App Members**:

```cpp
class App {
    // ... existing members ...

    // NEW: Phase 3 components
    views::ConsoleLog console_log_;
    views::ConsoleView console_view_;
    views::PlaybackState playback_state_;
    views::SignalInspector signal_inspector_;

    // NEW: Render methods
    void render_console();
    void render_inspector();
    // render_connection_status() extended with playback controls
};
```

**Updated Event Handling** (`handle_event()`):

```cpp
void App::handle_event(const std::string& json_str) {
    auto msg = nlohmann::json::parse(json_str, nullptr, false);
    if (msg.is_discarded()) return;

    auto type = msg.value("type", "");

    // Route ALL messages to console log
    console_log_.add_from_json(json_str, playback_state_.last_sim_time);

    // Update simulation state from events
    playback_state_.update_from_event(msg);

    if (type == "schema") {
        // ... existing schema handling (unchanged) ...
    } else if (type == "ack") {
        auto action = msg.value("action", "");
        if (action == "subscribe") {
            // ... existing subscribe ack handling (unchanged) ...
        }
        // Other ack types (pause, resume, step, set) now logged via console
    } else if (type == "event") {
        // State tracking handled above by playback_state_.update_from_event()
        // Event logging handled above by console_log_.add_from_json()
    } else if (type == "error") {
        // Error logging handled above by console_log_.add_from_json()
    } else if (type == "connection") {
        auto event = msg.value("event", "");
        if (event == "connected") {
            playback_state_.connected = true;
        } else if (event == "disconnected") {
            playback_state_.connected = false;
            playback_state_.reset();
        }
        // ... existing connection handling (unchanged) ...
    }
}
```

**Updated Telemetry Processing** (`process_telemetry()`):

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
            plot_manager_.set_current_time(hdr.time);

            // NEW: Update playback state with latest telemetry info
            playback_state_.update_from_telemetry(hdr.frame, hdr.time);
        }
    }
}
```

**Updated Docking Layout** (in `App::run()`):

```cpp
// Docking layout configuration
auto split_main_left = runner_params.dockingParams.dockingSplits.emplace_back();
split_main_left.initialDock = "MainDockSpace";
split_main_left.newDock = "SignalTreeSpace";
split_main_left.direction = ImGuiDir_Left;
split_main_left.ratio = 0.20f;

// NEW: Split signal tree space vertically for inspector
auto split_tree_inspector = runner_params.dockingParams.dockingSplits.emplace_back();
split_tree_inspector.initialDock = "SignalTreeSpace";
split_tree_inspector.newDock = "InspectorSpace";
split_tree_inspector.direction = ImGuiDir_Down;
split_tree_inspector.ratio = 0.50f;

// NEW: Split main space horizontally for console
auto split_plots_console = runner_params.dockingParams.dockingSplits.emplace_back();
split_plots_console.initialDock = "MainDockSpace";
split_plots_console.newDock = "ConsoleSpace";
split_plots_console.direction = ImGuiDir_Down;
split_plots_console.ratio = 0.30f;

// Dockable windows
runner_params.dockingParams.dockableWindows = {
    {"Signal Tree", "SignalTreeSpace", [this]() { render_signal_tree(); }},
    {"Plots",       "MainDockSpace",   [this]() { render_plot_workspace(); }},
    {"Console",     "ConsoleSpace",    [this]() { render_console(); }},       // NEW
    {"Inspector",   "InspectorSpace",  [this]() { render_inspector(); }},     // NEW
};
```

**Render Methods**:

```cpp
void App::render_console() {
    console_view_.render(console_log_);
}

void App::render_inspector() {
    signal_inspector_.render(subscribed_signals_, signal_buffers_, signal_units_);
}
```

**CMake Updates**:

```cmake
add_library(daedalus_lib STATIC
  src/daedalus/app.cpp
  src/daedalus/protocol/schema.cpp
  src/daedalus/protocol/client.cpp
  src/daedalus/data/signal_tree.cpp
  src/daedalus/views/plotter.cpp
  src/daedalus/views/console.cpp       # NEW
  src/daedalus/views/controls.cpp      # NEW
  src/daedalus/views/inspector.cpp     # NEW
)

add_executable(daedalus_tests
  tests/test_main.cpp
  tests/protocol/test_telemetry.cpp
  tests/protocol/test_schema.cpp
  tests/protocol/test_client.cpp
  tests/data/test_signal_buffer.cpp
  tests/data/test_signal_tree.cpp
  tests/data/test_telemetry_queue.cpp
  tests/views/test_plotter.cpp
  tests/views/test_console.cpp         # NEW
  tests/views/test_controls.cpp        # NEW
  tests/views/test_inspector.cpp       # NEW
)
```

**Replay Callback Wiring** (in `App::run()` or constructor):

```cpp
console_view_.set_replay_callback([this](const std::string& action,
                                          const nlohmann::json& params) {
    client_->send_command(action, params);
    console_log_.add_command(action, params);
});
```

**Reconnection Handling Updates**:
- On disconnect: `playback_state_.reset()`, `console_log_.add(System, "Disconnected")`
- On reconnect: `console_log_.add(System, "Connected")`, state resets to Unknown until first event

**Acceptance**: All Phase 3 components are wired into the App. Docking layout shows all four windows. Events flow from Hermes through the console. Transport controls send commands and log them. Inspector shows current values.

---

### Step 8: End-to-End Verification [ ]

**No new files** — manual verification.

**Procedure**:
1. Start Hermes: `python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml`
2. Build and run: `./scripts/build.sh && ./build/daedalus`
3. Verify playback controls:
   - [ ] Transport buttons visible in status bar
   - [ ] Initial state shows "Running" (telemetry streaming after auto-subscribe+resume)
   - [ ] Click "Pause" → simulation pauses, telemetry stops, state shows "Paused"
   - [ ] Click "Resume" → telemetry resumes, state shows "Running"
   - [ ] Click "Reset" → simulation resets, frame counter goes to 0
   - [ ] Set step count to 5, click "Step" → simulation advances 5 frames
   - [ ] Buttons correctly disabled based on state (Pause disabled when paused, etc.)
   - [ ] All buttons disabled when disconnected
4. Verify console:
   - [ ] Console window visible in bottom dock
   - [ ] Events appear: connected, subscribe ack, running/paused events
   - [ ] Commands appear when transport buttons are clicked
   - [ ] Type filter toggles work (hide/show events, acks, errors, commands)
   - [ ] Text filter narrows entries
   - [ ] Auto-scroll follows new entries
   - [ ] Hover over entry shows raw JSON tooltip
   - [ ] Right-click command entry → "Replay" re-sends the command
   - [ ] "Clear" button clears all entries
5. Verify inspector:
   - [ ] Inspector window visible in bottom-left dock
   - [ ] All subscribed signals listed with current values
   - [ ] Values update in real-time
   - [ ] Units displayed for signals that have them
   - [ ] Search filter narrows signal list
   - [ ] Column sorting works (click Signal header, click Value header)
   - [ ] Drag signal from inspector to plot works
6. Verify integration:
   - [ ] Pause via controls → plots freeze → console shows "[CMD] pause" then "[EVT] paused"
   - [ ] Resume → plots resume → console shows "[CMD] resume" then "[EVT] running"
   - [ ] Reset → frame/time in status bar reset to 0
   - [ ] Disconnect Hermes → all controls disabled, console shows "[SYS] Disconnected"
   - [ ] Reconnect → auto-subscribe, controls re-enabled, console shows reconnection
   - [ ] All Phase 2 features still work (drag-drop, multi-axis, cursor, stats)
7. Test with full sim: `python -m hermes.cli.main run references/hermes/examples/icarus_rocket.yaml`
   - [ ] ~90 signals load in inspector table
   - [ ] Inspector search filters 90 signals responsively
   - [ ] Console handles high event rate without lag
8. Run tests: `./scripts/test.sh` — all pass (Phase 1 + 2 + 3 tests)
   - [ ] `./scripts/test.sh` passes
9. Run CI: `./scripts/ci.sh` — clean build + all tests
   - [ ] `./scripts/ci.sh` passes

**Acceptance**: Full Phase 3 feature set works end-to-end against live Hermes data.

---

## 5. Dependency Graph

```
Step 1: Console data model                          ── no deps ──
Step 2: Simulation state tracking                   ── no deps ──
Step 3: Playback controls rendering                 ── depends on Step 2 ──
Step 4: Console view rendering                      ── depends on Step 1 ──
Step 5: Command history + replay                    ── depends on Steps 1, 4 ──
Step 6: Signal value inspector                      ── no deps ──
Step 7: App integration + docking layout            ── depends on Steps 3, 4, 5, 6 ──
Step 8: End-to-end verification                     ── depends on Step 7 ──
```

**Parallelizable**: Steps 1, 2, and 6 are fully independent. Steps 3 and 4 are independent of each other (3 depends on 2; 4 depends on 1). Step 6 can run in parallel with everything except Step 7.

**Recommended Session Flow**:
1. **Session A** — Data Models (Steps 1, 2, 6): All pure data structures + tests
2. **Session B** — Rendering (Steps 3, 4): Playback controls + console view
3. **Session C** — Polish + Integration (Steps 5, 7): Command replay + app wiring
4. **Session D** — Verification (Step 8): End-to-end testing

---

## 6. Directory Structure After Phase 3

```
include/daedalus/
├── app.hpp                       # Updated: +ConsoleLog, +PlaybackState, +Inspector
├── protocol/
│   ├── client.hpp                # Unchanged
│   ├── schema.hpp                # Unchanged
│   └── telemetry.hpp             # Unchanged
├── data/
│   ├── signal_buffer.hpp         # Unchanged
│   ├── signal_tree.hpp           # Unchanged
│   └── telemetry_queue.hpp       # Unchanged
└── views/
    ├── plotter.hpp               # Unchanged (Phase 2)
    ├── console.hpp               # NEW: ConsoleLog, ConsoleEntry, ConsoleView
    ├── controls.hpp              # NEW: SimulationState, PlaybackState, PlaybackAction
    └── inspector.hpp             # NEW: SignalInspector

src/daedalus/
├── main.cpp                      # Unchanged
├── app.cpp                       # Updated: event routing, docking, render methods
├── protocol/
│   ├── schema.cpp                # Unchanged
│   └── client.cpp                # Unchanged
├── data/
│   └── signal_tree.cpp           # Unchanged
└── views/
    ├── plotter.cpp               # Unchanged (Phase 2)
    ├── console.cpp               # NEW: Console rendering, filter bar, entry display
    ├── controls.cpp              # NEW: Playback controls rendering
    └── inspector.cpp             # NEW: Inspector table rendering

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
    ├── test_plotter.cpp          # Unchanged (Phase 2)
    ├── test_console.cpp          # NEW: Console data model tests
    ├── test_controls.cpp         # NEW: Simulation state tests
    └── test_inspector.cpp        # NEW: Inspector sort/filter tests
```

---

## 7. Performance Budget

### Per-Frame Costs (60 Hz, 16.67ms budget)

| Component | Est. Cost | Notes |
|:----------|:----------|:------|
| Queue drain | <0.5 ms | Same as Phase 1/2 |
| Frame decode + buffer push | <0.5 ms | Same as Phase 1/2 |
| Signal tree render | <1 ms | Same as Phase 1/2 |
| Plot rendering | ~3-5 ms | Same as Phase 2 |
| **Playback controls** | **<0.1 ms** | Few buttons + text, negligible |
| **Console render** | **<0.5 ms** | Scrollable region with clipper, ~20 visible entries |
| **Inspector table** | **<0.5 ms** | Table with clipper, ~30 visible rows |
| **State tracking** | **<0.01 ms** | Enum comparison, trivial |
| ImGui internals | ~5-8 ms | Widget rendering, text layout |
| **Total** | **~11-16 ms** | Within 16.67ms budget |

### Scaling Notes

- Console: `std::deque` with 1000 max entries. Rendering uses ImGui clipper — only visible entries are drawn regardless of total count.
- Inspector: With 90 signals (icarus_rocket), the table is trivially small. With 500 signals, the ImGui clipper handles virtualization automatically.
- The console `add_from_json()` parses JSON on the render thread but only for individual event messages (not telemetry), which arrive at low frequency (~1-10 per second for events, ~1 per control action).

---

## 8. Testing Strategy

### What We Test (Unit Tests)

| Area | Test Count | Description |
|:-----|:-----------|:------------|
| ConsoleLog data model | ~11 | Add entries, capacity, format_event/ack/error, clear |
| PlaybackState | ~11 | State transitions, enable/disable predicates, reset |
| SignalInspector | ~6 | Sort, filter, reset, empty case |

**Total new tests**: ~28 cases
**Running total**: 49 (Phase 1-2) + 28 = ~77 tests

### What We Don't Test (Manual Verification)

- ImGui button rendering
- Docking layout appearance
- Console scrolling behavior
- Inspector drag-drop interaction
- Actual WebSocket command delivery

### What We Verify End-to-End (Step 8)

Full interactive testing against live Hermes data:
- Transport controls send correct commands and state tracking works
- Console shows interleaved events and commands with correct formatting
- Inspector displays all signal values with search/sort
- All Phase 1+2 features remain functional

---

## 9. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| Status bar too crowded with controls | Controls don't fit | Medium | Use compact SmallButton, abbreviate labels. Fallback: move controls to a dockable toolbar |
| Console rendering with high event rate | FPS drops | Low | Max 1000 entries + ImGui clipper. Events are low-frequency (~10/s max) |
| Simulation state out of sync | Wrong button states | Low | State derived only from server events (authoritative). Reset on disconnect |
| Inspector sorting with 500+ signals | Brief stutter on sort | Low | Only re-sort when sort column/direction changes, cache sorted indices |
| HelloImGui docking 4-way split | Layout doesn't persist | Medium | Test docking splits early. HelloImGui persists layout in imgui.ini automatically |
| JSON parsing on render thread | FPS impact | Low | Only for event/control messages (~10/s), not telemetry. JSON parse is <0.01ms per message |
| Replay command while disconnected | Command lost | Low | Replay button disabled when disconnected (via controls_enabled check) |

---

## 10. Phase 3 Definition of Done

- [ ] `ConsoleLog` and `ConsoleEntry` data model with tests
- [ ] `SimulationState` enum and `PlaybackState` tracking with tests
- [ ] Transport buttons (Pause/Resume/Reset/Step) in status bar
- [ ] Buttons correctly enabled/disabled based on simulation state
- [ ] Commands dispatched to `HermesClient` on button click
- [ ] Console dockable window with scrollable event log
- [ ] Console entry type filtering (Events/Acks/Errors/Commands/System)
- [ ] Console text search filter
- [ ] Console auto-scroll to latest entry
- [ ] All events, acks, errors, and commands logged with timestamps
- [ ] Command history with right-click replay
- [ ] Signal value inspector dockable window
- [ ] Inspector shows all subscribed signals with current values and units
- [ ] Inspector text search filter
- [ ] Inspector column sorting (signal name, value)
- [ ] Docking layout: Signal Tree + Inspector (left), Plots (right-top), Console (right-bottom)
- [ ] Reconnection correctly resets state and console logs connection events
- [ ] All Phase 1+2 tests still pass (no regressions)
- [ ] ~28 new unit tests for Phase 3 data models
- [ ] `./scripts/ci.sh` passes (clean build + all tests)
- [ ] End-to-end verification against live Hermes data
- [ ] Works with both `websocket_telemetry.yaml` (4 signals) and `icarus_rocket.yaml` (~90 signals)
