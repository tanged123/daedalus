# Phase 4 Implementation Plan: Topology + Inspection

> **Created**: 2026-02-15
> **Branch**: `phase4`
> **Status**: Complete
> **Depends on**: Phase 3 (complete)

---

## 1. Phase 4 Objectives

Phase 4 adds system understanding to Daedalus. Where Phase 2 gave us eyes (plotting) and Phase 3 gave us hands (controls) and ears (event log), Phase 4 gives us a map — a visual diagram of how modules connect and data flows through the system. The deliverables from the bootstrap guide Section 16:

| # | Deliverable | Priority | Status |
|:--|:------------|:---------|:-------|
| 1 | imgui-node-editor integration (provided by ImGui Bundle) | High | Ready — library is compiled and linked |
| 2 | Auto-layout from schema wiring information | High | Ready — Hermes sends wiring in schema |
| 3 | Signal values displayed on hover over connections | Medium | Ready — live data available from signal buffers |
| 4 | Inspect command → shadow execution pipeline | Low | **Deferred** — requires Hermes protocol extension |
| 5 | Subgraph rendering with intermediate values | Low | **Deferred** — depends on Item 4 |

**Scope Note**: Items 4 and 5 require a new `inspect` action in the Hermes protocol that does not exist yet. This plan implements Items 1-3 fully, and adds the UI stubs + protocol proposal for Items 4-5 so the architecture is ready when Hermes adds support.

---

## 2. Architecture Overview

### 2.1 Component Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          App (render thread)                                │
│                                                                             │
│  ┌─────────────────┐          ┌────────────────────────────────────────┐   │
│  │ Signals          │          │  Plots / Topology (tabbed)            │   │
│  │ [Tree|Table]     │          │  ┌──────────┐ ┌──────────────────┐   │   │
│  │ (left dock)      │          │  │ PlotPanel │ │ Topology View    │   │   │
│  │                  │          │  └──────────┘ │ ┌────┐   ┌────┐  │   │   │
│  │                  │          │               │ │inp │──→│phys│  │   │   │
│  │                  │          │               │ └────┘   └────┘  │   │   │
│  │                  │          │               └──────────────────┘   │   │
│  └──────────────────┘          └────────────────────────────────────────┘   │
│                                                                             │
│                               ┌────────────────────────────────────────┐   │
│                               │  Console (bottom dock)                 │   │
│                               └────────────────────────────────────────┘   │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ Status Bar: Connected | Controls | State | Frame                    │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Data Flow: Schema → Topology

```
Hermes Schema (JSON)                    Topology Graph                     Node Editor
─────────────────────                   ───────────────                    ───────────
{                                       TopologyGraph {                   ┌─────────┐
  "modules": {                            nodes: [                        │ inputs  │
    "inputs": {...},                        {name:"inputs",               │ ○ thr.. ├─→┐
    "physics": {...}                           output_pins: [...]},       └─────────┘  │
  },                                        {name:"physics",             ┌─────────┐  │
  "wiring": [                                  input_pins: [...]}     ┌──┤ physics │  │
    {src:"inputs.thrust_cmd",             ],                          │  │ ○ in    ├←─┘
     dst:"physics.input"}                 links: [                    │  │ ○ out   │
  ]                                         {src_pin, dst_pin,        │  │ ○ state │
}                                            gain, offset}            │  └─────────┘
                                          ]                           │
                                        }                             │
                                                                      └── live values
                                                                          from SignalBuffers
```

### 2.3 New Files

```
include/daedalus/views/
├── topology.hpp           # TopologyGraph + TopologyView classes

src/daedalus/views/
├── topology.cpp           # Node editor rendering + layout algorithm

tests/views/
├── test_topology.cpp      # Graph construction + layout tests

tests/protocol/
├── test_schema.cpp        # Updated: wiring parsing tests
```

### 2.4 Modified Files

```
include/daedalus/protocol/schema.hpp    # Add WireInfo struct, wiring to Schema
src/daedalus/protocol/schema.cpp        # Parse wiring array from schema JSON
include/daedalus/app.hpp                # Add TopologyView member, render method
src/daedalus/app.cpp                    # Wire topology into docking, pass data
CMakeLists.txt                          # Add new source and test files
```

---

## 3. Design Decisions

### 3.1 Wiring Data Model

**Problem**: Hermes sends wiring information as part of the schema message, but Daedalus currently ignores it.

**Solution**: Extend `Schema` to include a `std::vector<WireInfo>` populated from the optional `"wiring"` array in the schema JSON.

```cpp
struct WireInfo {
    std::string src;        // "inputs.thrust_cmd" (module.signal)
    std::string dst;        // "physics.input"     (module.signal)
    double gain = 1.0;      // Multiplicative gain
    double offset = 0.0;    // Additive offset (applied after gain)
};

struct Schema {
    std::vector<ModuleInfo> modules;
    std::vector<WireInfo> wiring;  // NEW
};
```

**Protocol Reference**: The wiring array is optional — simple single-module configs (like `websocket_telemetry.yaml`) don't have it. Multi-module configs (like `multi_module.yaml` and `icarus_rocket.yaml`) include it. The topology view gracefully handles the no-wiring case by showing standalone module nodes.

### 3.2 Topology Graph as a Separate Data Model

**Problem**: Should the topology view build its graph directly from the schema each frame, or maintain a separate data model?

**Solution**: Maintain a `TopologyGraph` data model that is rebuilt when the schema changes. This separates data construction from rendering, enables unit testing of graph building/layout without ImGui, and caches layout positions across frames.

```cpp
struct TopologyGraph {
    std::vector<TopologyNode> nodes;
    std::vector<TopologyLink> links;

    void build_from_schema(const protocol::Schema& schema);
    void compute_layout();
    void update_subscription(const protocol::SubscribeAck& ack);
};
```

**Why not rebuild every frame?** Building the graph from the schema involves string parsing, ID assignment, and layout computation. These are O(N) operations that only need to run when the schema changes (once per connection), not every frame.

### 3.3 Node Editor ID Scheme

**Problem**: imgui-node-editor uses typed `NodeId`, `PinId`, and `LinkId` wrappers around `uintptr_t`. These must be unique across all nodes, pins, and links, and stable across frames.

**Solution**: Partition the ID space into non-overlapping ranges:

| Entity | ID Range | Formula |
|:-------|:---------|:--------|
| NodeId | 1 – 9,999 | `module_index + 1` |
| PinId | 10,001 – 99,999 | `10001 + (module_index * kMaxPinsPerModule) + pin_index` |
| LinkId | 100,001+ | `100001 + wire_index` |

With `kMaxPinsPerModule = 256`, this supports up to 350 modules with 256 pins each and 900,000 links. More than sufficient for any realistic simulation.

**Why not use pointer addresses?** imgui-node-editor supports pointer-as-ID, but our data structures may reallocate (invalidating pointers). Integer IDs computed from stable indices are safer.

### 3.4 Pin Layout: Wired Signals vs. All Signals

**Problem**: The `icarus_rocket.yaml` config has ~90 signals in the "rocket" module but only 1 wire. Showing all 90 signals as pins would create an enormous node that dominates the view.

**Solution**: Show only wired signals as actual pins. Unwired signals are summarized as a count label at the bottom of the node.

```
┌──────────────────────────┐
│         inputs           │
├──────────────────────────┤
│       thrust_cmd [N]   ○→│   ← Output pin (wired as source)
│       pitch_cmd [deg]    │   ← Unwired signal (label only)
└──────────────────────────┘
           │
           │ wire (gain=1.0)
           ▼
┌──────────────────────────┐
│         physics          │
├──────────────────────────┤
│←○ input                  │   ← Input pin (wired as destination)
│         output           │
│         state            │
└──────────────────────────┘
```

For modules with many unwired signals (>5), collapse to a summary:
```
│         ... 85 more      │
```

**Why this tradeoff?** The topology view's purpose is showing data flow between modules. The signal tree already provides a complete signal listing. Showing all signals as pins would make the topology view a worse version of the signal tree rather than a complementary view.

### 3.5 Auto-Layout Algorithm

**Problem**: Nodes need initial positions that show data flow clearly. Users should also be able to drag nodes freely.

**Solution**: A simple layered layout based on topological ordering of the wiring DAG:

1. **Build adjacency**: From wiring, create module → module edges
2. **Assign layers**: BFS from source nodes (modules with no incoming wires). Layer = longest path from any source.
3. **Position**: `x = layer * kHorizontalSpacing`, `y = index_within_layer * kVerticalSpacing`
4. **Center**: Offset so the graph is centered in the editor

```
Layer 0          Layer 1          Layer 2
┌────────┐       ┌────────┐       ┌────────┐
│ inputs │──────→│ physics│──────→│ output │
└────────┘       └────────┘       └────────┘
                 ┌────────┐
                 │ sensors│  (standalone, layer 0)
                 └────────┘
```

**Layout constants**:
- `kHorizontalSpacing = 300.0f` (pixels between layers)
- `kVerticalSpacing = 200.0f` (pixels between nodes in same layer)

**Re-layout**: A "Reset Layout" button in the topology toolbar re-runs the algorithm. Node positions are otherwise persistent (imgui-node-editor saves them).

**Why not force-directed?** Force-directed layout is iterative and non-deterministic. A simple layered layout produces clean, readable results for pipeline-style module graphs (which is what simulation wiring typically looks like) and runs in O(V+E) time.

### 3.6 Live Signal Values on Hover

**Problem**: How to show current signal values in the topology view without cluttering the diagram.

**Solution**: Show values on hover using ImGui tooltips:

1. **Hover on pin**: Tooltip shows `signal_path = value [unit]`
2. **Hover on link**: Tooltip shows `src → dst`, `value`, `gain`, `offset`
3. **Animated flow**: Use `ed::Flow(linkId)` to animate data flow direction on links when telemetry is active

```
Hover on output pin "thrust_cmd":
┌──────────────────────────────┐
│ inputs.thrust_cmd = 100.5 N  │
└──────────────────────────────┘

Hover on wire:
┌──────────────────────────────────────┐
│ inputs.thrust_cmd → physics.input    │
│ Value: 100.5 N                       │
│ Gain: 1.0  Offset: 0.0              │
└──────────────────────────────────────┘
```

**Why tooltips, not inline labels?** With many wires, inline labels create visual clutter. Tooltips show detail on demand while keeping the diagram clean.

### 3.7 Docking Placement: Tabbed with Plots

**Problem**: Where should the topology view live in the docking layout?

**Decision**: The topology view shares the **MainDockSpace** with the Plots window as a tabbed pair. The user can switch between Plots and Topology tabs, or undock either to see both.

**Why tabbed?** The topology view and plots serve different purposes — you typically look at topology to understand structure, then switch to plots to observe signals. They rarely need to be viewed simultaneously. Tabbing maximizes available space for each view.

**Layout update**:
```
 ___________________________________________
 |              |                           |
 | Signals      |  [Plots] [Topology]       |  ← Tabbed
 | [Tree|Table] |    (main dock space)      |
 | (left 20%)   |                           |
 |              |---------------------------|
 |              |    Console                |
 |              |    (bottom 30%)           |
 -------------------------------------------
 | Status Bar                               |
 -------------------------------------------
```

### 3.8 Inspect Command: Protocol Proposal (Deferred)

**Problem**: Items 4 and 5 from the bootstrap guide require an `inspect` command that triggers shadow execution in Hermes. This protocol extension doesn't exist yet.

**Proposed Protocol Extension**:

```json
// Client → Server
{"action": "inspect", "params": {"module": "physics"}}

// Server → Client (proposed response)
{
  "type": "ack",
  "action": "inspect",
  "module": "physics",
  "execution_path": ["inputs", "physics"],
  "intermediate_values": {
    "inputs.thrust_cmd": 100.5,
    "physics.input": 100.5,
    "physics.output": 42.3,
    "physics.state": 0.89
  }
}
```

**Implementation Approach**: Add a right-click context menu on module nodes with an "Inspect" option. When clicked, send the `inspect` command. Hermes will currently return `{"type": "error", "message": "Unknown action: inspect"}`, which the console will display. When Hermes adds support, the response will be handled to highlight the execution path and display intermediate values.

**Why not block on this?** Items 1-3 are independently valuable — showing the module topology with live values covers the primary use case. Inspect mode is an advanced debugging feature that can be added incrementally when the server supports it.

### 3.9 Node Editor Context Lifecycle

**Problem**: `ax::NodeEditor::EditorContext` must be created once and reused across frames. Creating/destroying it each frame is incorrect and causes crashes.

**Solution**: The `TopologyView` class owns the `EditorContext*`, creating it in the constructor and destroying it in the destructor. The context is set as current at the beginning of each `render()` call.

```cpp
class TopologyView {
public:
    TopologyView();   // Creates editor context
    ~TopologyView();  // Destroys editor context
    // ...
private:
    ax::NodeEditor::EditorContext* context_ = nullptr;
};
```

**Why RAII?** The editor context contains internal state (node positions, selection, undo history). It must persist across frames. RAII ensures correct cleanup even if exceptions propagate.

---

## 4. Implementation Steps

### Step 1: Wiring Data Model + Schema Parsing

**Files**: `include/daedalus/protocol/schema.hpp`, `src/daedalus/protocol/schema.cpp`, `tests/protocol/test_schema.cpp`

**Why first**: Pure data structures and parsing. Everything else depends on having the wiring data available from the schema.

**Schema Header Changes** (`schema.hpp`):

```cpp
namespace daedalus::protocol {

// ... existing SignalInfo, ModuleInfo ...

/// A wire connecting two signals across modules.
struct WireInfo {
    std::string src;        // Qualified source: "module.signal"
    std::string dst;        // Qualified destination: "module.signal"
    double gain = 1.0;
    double offset = 0.0;
};

struct Schema {
    std::vector<ModuleInfo> modules;
    std::vector<WireInfo> wiring;  // NEW: populated from optional "wiring" array
};

// ... existing parse_schema, parse_subscribe_ack ...

} // namespace daedalus::protocol
```

**Schema Parsing Changes** (`schema.cpp`):

```cpp
Schema parse_schema(const nlohmann::json& msg) {
    // ... existing module parsing (unchanged) ...

    // NEW: Parse optional wiring array
    if (msg.contains("wiring") && msg["wiring"].is_array()) {
        for (const auto& wire_json : msg["wiring"]) {
            WireInfo wire;
            if (!wire_json.contains("src") || !wire_json["src"].is_string()) continue;
            if (!wire_json.contains("dst") || !wire_json["dst"].is_string()) continue;
            wire.src = wire_json["src"].get<std::string>();
            wire.dst = wire_json["dst"].get<std::string>();
            wire.gain = wire_json.value("gain", 1.0);
            wire.offset = wire_json.value("offset", 0.0);
            schema.wiring.push_back(std::move(wire));
        }
    }

    return schema;
}
```

**Key Design Notes**:
- Wiring is optional — schemas without `"wiring"` produce an empty vector
- Malformed wire entries (missing `src` or `dst`) are silently skipped rather than throwing, since the wiring is informational (doesn't affect telemetry flow)
- `gain` defaults to 1.0, `offset` defaults to 0.0 (matching Hermes server behavior)

**Tests** (`tests/protocol/test_schema.cpp` — extend existing):
- Parse schema with wiring: verify wire count, src, dst, gain, offset
- Parse schema without wiring: verify empty wiring vector
- Parse schema with wiring missing gain/offset: verify defaults (1.0, 0.0)
- Parse schema with non-default gain/offset: verify values parsed correctly
- Parse schema with malformed wire entry (missing src): verify it's skipped
- Parse schema with empty wiring array: verify empty vector

**Acceptance**: Schema parsing extracts wiring information. No regressions in existing schema/module parsing tests.

---

### Step 2: Topology Graph Data Model

**Files**: `include/daedalus/views/topology.hpp`, `tests/views/test_topology.cpp`

**Why second**: Pure data structures with no rendering. The graph model is the foundation that the view (Step 3) and layout (Step 4) build on.

**Definitions**:

```cpp
namespace daedalus::views {

/// A pin (signal endpoint) on a topology node.
struct TopologyPin {
    uintptr_t id;                       // Unique ID for imgui-node-editor
    std::string signal_path;            // "inputs.thrust_cmd"
    std::string signal_name;            // "thrust_cmd"
    ax::NodeEditor::PinKind kind;       // Input or Output
    std::optional<size_t> signal_index; // From subscribe ack (for live values)
    std::optional<std::string> unit;    // Signal unit if available
};

/// A module node in the topology graph.
struct TopologyNode {
    uintptr_t id;                       // Unique ID for imgui-node-editor
    std::string module_name;            // "inputs"
    ImVec2 position = {0, 0};          // Layout position (pixels)
    std::vector<TopologyPin> input_pins;    // Wired as destination
    std::vector<TopologyPin> output_pins;   // Wired as source
    size_t unwired_signal_count = 0;    // Count of signals not in any wire
};

/// A wire (link) between two pins.
struct TopologyLink {
    uintptr_t id;                       // Unique ID for imgui-node-editor
    uintptr_t source_pin_id;            // Output pin ID
    uintptr_t dest_pin_id;              // Input pin ID
    std::string source_signal;          // "inputs.thrust_cmd"
    std::string dest_signal;            // "physics.input"
    double gain = 1.0;
    double offset = 0.0;
};

/// The complete topology graph built from a Hermes schema.
class TopologyGraph {
  public:
    /// Build graph from a parsed schema.
    /// Creates nodes for each module, pins for wired signals, links for wires.
    void build_from_schema(const protocol::Schema& schema);

    /// Compute auto-layout positions using layered layout algorithm.
    void compute_layout();

    /// Assign signal indices to pins based on subscribe ack.
    /// Enables live value display for pins that match subscribed signals.
    void update_subscription(const protocol::SubscribeAck& ack);

    /// Assign signal units to pins from the unit map.
    void update_units(const std::unordered_map<std::string, std::string>& units);

    /// Clear the graph.
    void clear();

    /// Accessors.
    const std::vector<TopologyNode>& nodes() const { return nodes_; }
    const std::vector<TopologyLink>& links() const { return links_; }
    bool empty() const { return nodes_.empty(); }
    bool has_wiring() const { return !links_.empty(); }

  private:
    std::vector<TopologyNode> nodes_;
    std::vector<TopologyLink> links_;

    /// ID generation constants.
    static constexpr uintptr_t kNodeIdBase = 1;
    static constexpr uintptr_t kPinIdBase = 10001;
    static constexpr uintptr_t kMaxPinsPerModule = 256;
    static constexpr uintptr_t kLinkIdBase = 100001;
};

} // namespace daedalus::views
```

**Graph Building Algorithm** (`build_from_schema`):

```
1. Clear existing graph
2. For each module in schema:
   a. Create TopologyNode with ID = kNodeIdBase + module_index
   b. Collect set of signals that appear as wire src/dst for this module
3. For each wire in schema.wiring:
   a. Find source module node, add output pin if not already present
   b. Find dest module node, add input pin if not already present
   c. Create TopologyLink connecting the two pins
4. For each module, count unwired signals (total signals - wired signals)
5. Call compute_layout() to assign initial positions
```

**Key Design Notes**:
- Pins are deduplicated: if the same signal appears in multiple wires (as source), it gets one pin
- Pin IDs are deterministic: `kPinIdBase + module_index * kMaxPinsPerModule + pin_index`
- A signal can have both an input pin and an output pin (if it receives data from one wire and feeds into another)
- Modules with no wires get standalone nodes (no pins, positioned in layer 0)

**Tests** (`tests/views/test_topology.cpp`):
- Build from schema with wiring: correct node count, link count, pin count
- Build from schema without wiring: nodes exist, no links, no pins
- Build from schema with empty modules: empty graph
- Pin direction: source signal gets Output pin, dest signal gets Input pin
- Link connects correct pin IDs
- Wire gain/offset preserved in link
- Unwired signal count is correct (total - wired)
- Signal that appears in multiple wires gets only one pin
- `clear()` empties the graph
- `has_wiring()` returns true iff links exist
- `update_subscription()` assigns signal indices to matching pins
- `update_units()` assigns units to matching pins

**Acceptance**: TopologyGraph correctly builds from schema data. All graph construction is testable without any ImGui dependencies.

---

### Step 3: Auto-Layout Algorithm

**Files**: `src/daedalus/views/topology.cpp` (implement `compute_layout`), `tests/views/test_topology.cpp` (extend)

**Why third**: Layout depends on the graph model (Step 2). It's pure geometry, testable without rendering.

**Algorithm** (`compute_layout`):

```cpp
void TopologyGraph::compute_layout() {
    if (nodes_.empty()) return;

    // Step 1: Build module adjacency from links
    // adjacency[src_module_index] = {dst_module_indices...}
    std::unordered_map<size_t, std::vector<size_t>> adjacency;
    std::unordered_map<size_t, size_t> in_degree;

    // Map module name → node index
    std::unordered_map<std::string, size_t> name_to_index;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        name_to_index[nodes_[i].module_name] = i;
        in_degree[i] = 0;
    }

    for (const auto& link : links_) {
        // Extract module names from signal paths
        auto src_mod = link.source_signal.substr(0, link.source_signal.find('.'));
        auto dst_mod = link.dest_signal.substr(0, link.dest_signal.find('.'));
        auto src_it = name_to_index.find(src_mod);
        auto dst_it = name_to_index.find(dst_mod);
        if (src_it != name_to_index.end() && dst_it != name_to_index.end()) {
            adjacency[src_it->second].push_back(dst_it->second);
            in_degree[dst_it->second]++;
        }
    }

    // Step 2: Assign layers via BFS from sources (in_degree == 0)
    std::vector<int> layer(nodes_.size(), 0);
    std::queue<size_t> queue;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (in_degree[i] == 0) {
            queue.push(i);
        }
    }

    while (!queue.empty()) {
        size_t node = queue.front();
        queue.pop();
        for (size_t neighbor : adjacency[node]) {
            layer[neighbor] = std::max(layer[neighbor], layer[node] + 1);
            if (--in_degree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    // Step 3: Group nodes by layer and assign positions
    constexpr float kHorizontalSpacing = 300.0f;
    constexpr float kVerticalSpacing = 200.0f;

    std::map<int, std::vector<size_t>> layers;
    for (size_t i = 0; i < nodes_.size(); ++i) {
        layers[layer[i]].push_back(i);
    }

    for (auto& [layer_idx, node_indices] : layers) {
        float total_height = (node_indices.size() - 1) * kVerticalSpacing;
        float start_y = -total_height / 2.0f;
        for (size_t i = 0; i < node_indices.size(); ++i) {
            nodes_[node_indices[i]].position = {
                layer_idx * kHorizontalSpacing,
                start_y + i * kVerticalSpacing
            };
        }
    }
}
```

**Tests** (extend `tests/views/test_topology.cpp`):
- Single module: positioned at origin
- Two modules with one wire: source at x=0, dest at x=300
- Chain of three modules: layers 0, 1, 2
- Two modules in same layer: offset vertically, centered
- Module with no wires: placed in layer 0
- Diamond pattern (A→B, A→C, B→D, C→D): D at layer 2
- Layout after `compute_layout()` produces non-overlapping positions

**Acceptance**: Nodes are positioned in a readable left-to-right layout. Source modules on the left, consumers on the right.

---

### Step 4: Topology View Rendering

**Files**: `include/daedalus/views/topology.hpp` (extend), `src/daedalus/views/topology.cpp`

**Why fourth**: Depends on graph model (Step 2) and layout (Step 3). This is the primary rendering step.

**TopologyView Class**:

```cpp
namespace daedalus::views {

/// Renders the topology graph using imgui-node-editor.
class TopologyView {
  public:
    TopologyView();
    ~TopologyView();

    TopologyView(const TopologyView&) = delete;
    TopologyView& operator=(const TopologyView&) = delete;

    /// Render the topology view.
    /// Call inside an ImGui window (dockable window set up by App).
    void render(const TopologyGraph& graph,
                const std::map<size_t, data::SignalBuffer>& signal_buffers);

    /// Reset the view (navigate to content).
    void reset();

    /// Re-apply auto-layout positions to the editor.
    void apply_layout(const TopologyGraph& graph);

  private:
    ax::NodeEditor::EditorContext* context_ = nullptr;
    bool first_frame_ = true;   // Navigate to content on first render
    bool layout_dirty_ = true;  // Apply layout positions on next render

    /// Render a single module node with its pins.
    void render_node(const TopologyNode& node,
                     const std::map<size_t, data::SignalBuffer>& signal_buffers);

    /// Render a single pin (input or output).
    void render_pin(const TopologyPin& pin,
                    const std::map<size_t, data::SignalBuffer>& signal_buffers);

    /// Render all links between pins.
    void render_links(const TopologyGraph& graph,
                      const std::map<size_t, data::SignalBuffer>& signal_buffers);

    /// Render the toolbar (reset layout button, etc.).
    void render_toolbar();

    /// Handle hover tooltips for pins and links.
    void handle_hover_tooltips(const TopologyGraph& graph,
                               const std::map<size_t, data::SignalBuffer>& signal_buffers);
};

} // namespace daedalus::views
```

**Rendering Implementation**:

```cpp
TopologyView::TopologyView() {
    ax::NodeEditor::Config config;
    config.SettingsFile = nullptr;  // Don't persist to file
    context_ = ax::NodeEditor::CreateEditor(&config);
}

TopologyView::~TopologyView() {
    ax::NodeEditor::DestroyEditor(context_);
}

void TopologyView::render(const TopologyGraph& graph,
                          const std::map<size_t, data::SignalBuffer>& signal_buffers) {
    render_toolbar();

    if (graph.empty()) {
        ImGui::TextDisabled("No schema received. Waiting for connection...");
        return;
    }

    if (!graph.has_wiring()) {
        ImGui::TextDisabled("No wiring information in schema.");
        ImGui::TextDisabled("Module topology requires wiring configuration.");
        return;
    }

    ax::NodeEditor::SetCurrentEditor(context_);
    ax::NodeEditor::Begin("TopologyEditor");

    // Apply layout positions (only when dirty)
    if (layout_dirty_) {
        apply_layout(graph);
        layout_dirty_ = false;
    }

    // Render nodes
    for (const auto& node : graph.nodes()) {
        render_node(node, signal_buffers);
    }

    // Render links
    render_links(graph, signal_buffers);

    // Handle hover tooltips (must be inside ed::Begin/End)
    handle_hover_tooltips(graph, signal_buffers);

    ax::NodeEditor::End();

    // Navigate to content on first frame
    if (first_frame_) {
        ax::NodeEditor::NavigateToContent(0.0f);
        first_frame_ = false;
    }

    ax::NodeEditor::SetCurrentEditor(nullptr);
}
```

**Node Rendering**:

```cpp
void TopologyView::render_node(const TopologyNode& node,
                               const std::map<size_t, data::SignalBuffer>& buffers) {
    ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(node.id));

    // Title
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.2f, 1.0f));
    ImGui::TextUnformatted(node.module_name.c_str());
    ImGui::PopStyleColor();

    ImGui::Separator();

    // Input pins (left side)
    for (const auto& pin : node.input_pins) {
        render_pin(pin, buffers);
    }

    // Output pins (right side)
    for (const auto& pin : node.output_pins) {
        render_pin(pin, buffers);
    }

    // Unwired signal count
    if (node.unwired_signal_count > 0) {
        ImGui::TextDisabled("... %zu more", node.unwired_signal_count);
    }

    ax::NodeEditor::EndNode();
}
```

**Pin Rendering**:

```cpp
void TopologyView::render_pin(const TopologyPin& pin,
                              const std::map<size_t, data::SignalBuffer>& buffers) {
    ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(pin.id), pin.kind);

    if (pin.kind == ax::NodeEditor::PinKind::Input) {
        // Input: icon on left
        ImGui::Text("-> %s", pin.signal_name.c_str());
    } else {
        // Output: icon on right
        ImGui::Text("%s ->", pin.signal_name.c_str());
    }

    ax::NodeEditor::EndPin();

    // Show value on same line if subscribed
    if (pin.signal_index.has_value()) {
        auto it = buffers.find(pin.signal_index.value());
        if (it != buffers.end() && !it->second.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%.4g", it->second.last_value());
        }
    }
}
```

**Link Rendering**:

```cpp
void TopologyView::render_links(const TopologyGraph& graph,
                                const std::map<size_t, data::SignalBuffer>& buffers) {
    for (const auto& link : graph.links()) {
        ImVec4 color = ImVec4(0.5f, 0.8f, 0.5f, 1.0f);  // Green for active
        ax::NodeEditor::Link(
            ax::NodeEditor::LinkId(link.id),
            ax::NodeEditor::PinId(link.source_pin_id),
            ax::NodeEditor::PinId(link.dest_pin_id),
            color, 2.0f);

        // Animate flow when telemetry is active
        if (!buffers.empty()) {
            ax::NodeEditor::Flow(ax::NodeEditor::LinkId(link.id));
        }
    }
}
```

**Toolbar**:

```cpp
void TopologyView::render_toolbar() {
    if (ImGui::SmallButton("Reset Layout")) {
        layout_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit to View")) {
        if (context_) {
            ax::NodeEditor::SetCurrentEditor(context_);
            ax::NodeEditor::NavigateToContent(0.35f);
            ax::NodeEditor::SetCurrentEditor(nullptr);
        }
    }
    ImGui::Separator();
}
```

**Key Details**:
- `SettingsFile = nullptr` prevents imgui-node-editor from writing a separate `NodeEditor.json` file
- `NavigateToContent()` on first frame ensures the graph is visible without manual scrolling
- `Flow()` animates data flow markers along links — gives visual indication that telemetry is active
- `layout_dirty_` flag avoids re-applying positions every frame (which would override user dragging)
- Node positions are set via `SetNodePosition()` in `apply_layout()`, then managed by the editor

**Acceptance**: Topology renders module nodes with pins and wire links. Nodes are draggable. Links animate when telemetry is flowing.

---

### Step 5: Signal Values on Hover

**Files**: `src/daedalus/views/topology.cpp` (extend `handle_hover_tooltips`)

**Why fifth**: Depends on the rendering (Step 4) being in place. Adds interactivity without changing the data model.

**Implementation**:

```cpp
void TopologyView::handle_hover_tooltips(
    const TopologyGraph& graph,
    const std::map<size_t, data::SignalBuffer>& buffers) {

    // Check for hovered link
    ax::NodeEditor::LinkId hovered_link = ax::NodeEditor::GetHoveredLink();
    if (hovered_link) {
        // Find the link in our data
        for (const auto& link : graph.links()) {
            if (link.id == hovered_link.Get()) {
                ax::NodeEditor::Suspend();
                ImGui::BeginTooltip();

                ImGui::Text("%s", link.source_signal.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled("->");
                ImGui::SameLine();
                ImGui::Text("%s", link.dest_signal.c_str());

                // Show current value if available
                // Look up source signal's buffer index
                for (const auto& node : graph.nodes()) {
                    for (const auto& pin : node.output_pins) {
                        if (pin.signal_path == link.source_signal &&
                            pin.signal_index.has_value()) {
                            auto it = buffers.find(pin.signal_index.value());
                            if (it != buffers.end() && !it->second.empty()) {
                                ImGui::Separator();
                                ImGui::Text("Value: %.6g", it->second.last_value());
                                if (pin.unit.has_value()) {
                                    ImGui::SameLine();
                                    ImGui::TextDisabled("%s", pin.unit->c_str());
                                }
                            }
                        }
                    }
                }

                if (link.gain != 1.0 || link.offset != 0.0) {
                    ImGui::Separator();
                    ImGui::TextDisabled("Gain: %.4g  Offset: %.4g",
                                       link.gain, link.offset);
                }

                ImGui::EndTooltip();
                ax::NodeEditor::Resume();
                break;
            }
        }
    }

    // Check for hovered pin
    ax::NodeEditor::PinId hovered_pin = ax::NodeEditor::GetHoveredPin();
    if (hovered_pin) {
        for (const auto& node : graph.nodes()) {
            auto show_pin_tooltip = [&](const TopologyPin& pin) {
                if (pin.id == hovered_pin.Get()) {
                    ax::NodeEditor::Suspend();
                    ImGui::BeginTooltip();
                    ImGui::Text("%s", pin.signal_path.c_str());

                    if (pin.signal_index.has_value()) {
                        auto it = buffers.find(pin.signal_index.value());
                        if (it != buffers.end() && !it->second.empty()) {
                            ImGui::SameLine();
                            ImGui::Text("= %.6g", it->second.last_value());
                            if (pin.unit.has_value()) {
                                ImGui::SameLine();
                                ImGui::TextDisabled("%s", pin.unit->c_str());
                            }
                        }
                    }

                    ImGui::EndTooltip();
                    ax::NodeEditor::Resume();
                    return true;
                }
                return false;
            };

            bool found = false;
            for (const auto& pin : node.input_pins) {
                if (show_pin_tooltip(pin)) { found = true; break; }
            }
            if (!found) {
                for (const auto& pin : node.output_pins) {
                    if (show_pin_tooltip(pin)) break;
                }
            }
        }
    }
}
```

**Key Details**:
- `ed::Suspend()` / `ed::Resume()` must wrap ImGui tooltip rendering inside the node editor — this is required by the imgui-node-editor API when rendering non-editor widgets inside the editor scope
- Link tooltips show source → destination, current value, and gain/offset (if non-default)
- Pin tooltips show the full signal path and current value with unit
- Value lookup uses the pin's `signal_index` → `signal_buffers_` map

**Acceptance**: Hovering over pins shows signal path and current value. Hovering over links shows wire details and value. Values update in real-time.

---

### Step 6: Inspect Mode Stub

**Files**: `src/daedalus/views/topology.cpp` (extend), `include/daedalus/views/topology.hpp` (extend)

**Why sixth**: Adds the context menu for future inspect functionality. Depends on rendering (Step 4).

**Context Menu**:

```cpp
// Inside render(), after links, before End():
ax::NodeEditor::NodeId context_node_id;
ax::NodeEditor::Suspend();
if (ax::NodeEditor::ShowNodeContextMenu(&context_node_id)) {
    ImGui::OpenPopup("NodeContextMenu");
    context_node_id_ = context_node_id;  // Store for popup handler
}
if (ImGui::BeginPopup("NodeContextMenu")) {
    // Find module name for display
    std::string module_name = "Unknown";
    for (const auto& node : graph.nodes()) {
        if (node.id == context_node_id_.Get()) {
            module_name = node.module_name;
            break;
        }
    }
    ImGui::TextDisabled("Module: %s", module_name.c_str());
    ImGui::Separator();

    if (ImGui::MenuItem("Inspect (requires Hermes support)")) {
        // Send inspect command — Hermes will return an error for now
        if (inspect_callback_) {
            inspect_callback_(module_name);
        }
    }
    if (ImGui::MenuItem("Navigate to Content")) {
        ax::NodeEditor::NavigateToContent(0.35f);
    }

    ImGui::EndPopup();
}
ax::NodeEditor::Resume();
```

**Callback Interface**:

```cpp
class TopologyView {
  public:
    using InspectCallback = std::function<void(const std::string& module_name)>;
    void set_inspect_callback(InspectCallback cb);

  private:
    InspectCallback inspect_callback_;
    ax::NodeEditor::NodeId context_node_id_;
};
```

**App Integration** (wired in Step 7):

```cpp
topology_view_.set_inspect_callback([this](const std::string& module_name) {
    client_->send_command("inspect", {{"module", module_name}});
    console_log_.add_command("inspect", {{"module", module_name}});
});
```

**Acceptance**: Right-clicking a module node shows a context menu with "Inspect" option. Clicking it sends the command (which Hermes currently rejects with an error displayed in the console).

---

### Step 7: App Integration + Docking Layout

**Files**: `include/daedalus/app.hpp`, `src/daedalus/app.cpp`, `CMakeLists.txt`

**Why seventh**: Final integration step. Wires all new components into the App lifecycle.

**New App Members**:

```cpp
class App {
    // ... existing members ...

    // NEW: Phase 4 components
    views::TopologyGraph topology_graph_;
    views::TopologyView topology_view_;

    // NEW: Render method
    void render_topology();
};
```

**Updated Event Handling** (`handle_event()` changes):

```cpp
if (type == "schema") {
    current_schema_ = protocol::parse_schema(msg);
    signal_tree_.build_from_schema(current_schema_);

    // ... existing unit extraction ...

    // NEW: Build topology from schema
    topology_graph_.build_from_schema(current_schema_);
    topology_graph_.update_units(signal_units_);

    schema_received_ = true;
    // ... existing subscribe logic ...

} else if (type == "ack") {
    const std::string action = msg.value("action", "");
    if (action == "subscribe") {
        auto ack = protocol::parse_subscribe_ack(msg);
        signal_tree_.update_subscription(ack);

        // NEW: Update topology pin indices
        topology_graph_.update_subscription(ack);

        // ... existing buffer creation + resume ...
    }
} else if (type == "connection") {
    // ... existing connection handling ...
    if (event == "disconnected") {
        // ... existing resets ...
        topology_graph_.clear();       // NEW
        topology_view_.reset();        // NEW
    }
}
```

**Updated Docking Layout**:

```cpp
// Define dockable windows (Topology tabs with Plots)
HelloImGui::DockableWindow topology_window;
topology_window.label = "Topology";
topology_window.dockSpaceName = "MainDockSpace";
topology_window.GuiFunction = [this] { render_topology(); };

runner_params.dockingParams.dockableWindows = {
    signals_window,
    plots_window,
    topology_window,    // NEW: tabbed with Plots
    console_window
};
```

**Render Method**:

```cpp
void App::render_topology() {
    topology_view_.render(topology_graph_, signal_buffers_);
}
```

**Inspect Callback Wiring** (in `App::run()` after client creation):

```cpp
topology_view_.set_inspect_callback([this](const std::string& module_name) {
    if (client_ && playback_state_.connected) {
        client_->send_command("inspect", {{"module", module_name}});
        console_log_.add_command("inspect", {{"module", module_name}});
    } else {
        console_log_.add(views::ConsoleEntryType::System,
                         "Inspect skipped: not connected", "",
                         playback_state_.last_sim_time);
    }
});
```

**CMake Updates**:

```cmake
add_library(daedalus_lib STATIC
  # ... existing sources ...
  src/daedalus/views/topology.cpp       # NEW
)

add_executable(daedalus_tests
  # ... existing tests ...
  tests/views/test_topology.cpp         # NEW
)
```

**Acceptance**: Topology view appears as a tab alongside Plots. Schema data flows to the topology graph. Live values appear in pin tooltips. Inspect callback sends commands. Reconnection rebuilds the graph.

---

### Step 8: End-to-End Verification

**No new files** — manual verification.

**Procedure**:

1. **Simple config** — Start Hermes with `websocket_telemetry.yaml` (single module, no wiring):
   ```bash
   python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml
   ```
   - [ ] Build and run: `./scripts/build.sh && ./build/daedalus`
   - [ ] Topology tab visible alongside Plots in the main dock
   - [ ] Topology shows "No wiring information" message (this config has no wiring)
   - [ ] All Phase 1-3 features still work (plotting, controls, console, inspector)

2. **Multi-module config** — Start Hermes with `multi_module.yaml` (wiring present):
   ```bash
   python -m hermes.cli.main run references/hermes/examples/multi_module.yaml
   ```
   - [ ] Topology shows two module nodes: "inputs" and "physics"
   - [ ] Wire visible from `inputs.thrust_cmd` → `physics.input`
   - [ ] Nodes are laid out left-to-right (inputs on left, physics on right)
   - [ ] Animated flow markers on the wire when telemetry is streaming
   - [ ] Hover on the wire shows: source → dest, current value, gain/offset
   - [ ] Hover on a pin shows signal path and current value
   - [ ] Nodes are draggable
   - [ ] "Reset Layout" button repositions nodes to auto-layout
   - [ ] "Fit to View" button navigates to show all content
   - [ ] "inputs" node shows `thrust_cmd` as output pin, `pitch_cmd` with no pin (or as unwired)
   - [ ] "physics" node shows `input` as input pin, `output` and `state` with no pins
   - [ ] Right-click module → "Inspect" sends command, console shows error from Hermes

3. **Icarus rocket config** (if available):
   ```bash
   python -m hermes.cli.main run references/hermes/examples/icarus_rocket.yaml
   ```
   - [ ] Two modules visible: "inputs" and "rocket"
   - [ ] Wire from `inputs.throttle` → `rocket.Rocket.Engine.throttle_cmd`
   - [ ] Rocket node shows "~88 more" for unwired signals
   - [ ] Values update in real-time on hover

4. **Integration testing**:
   - [ ] Switch between Plots and Topology tabs smoothly
   - [ ] Pause simulation → flow animation stops on links
   - [ ] Resume → flow animation resumes
   - [ ] Reset → topology rebuilds (if schema re-sent)
   - [ ] Disconnect Hermes → topology shows waiting message
   - [ ] Reconnect → topology rebuilds from new schema
   - [ ] All Phase 1-3 features still work (no regressions)

5. **Test suite**:
   - [x] `./scripts/test.sh` passes (all Phase 1-4 tests)
   - [x] `./scripts/ci.sh` passes (clean build + all tests)

**Acceptance**: Full Phase 4 feature set works end-to-end against live Hermes data with wiring.

---

## 5. Dependency Graph

```
Step 1: Wiring data model + schema parsing      ── no deps ──
Step 2: Topology graph data model                ── depends on Step 1 ──
Step 3: Auto-layout algorithm                    ── depends on Step 2 ──
Step 4: Topology view rendering                  ── depends on Steps 2, 3 ──
Step 5: Signal values on hover                   ── depends on Step 4 ──
Step 6: Inspect mode stub                        ── depends on Step 4 ──
Step 7: App integration + docking layout         ── depends on Steps 4, 5, 6 ──
Step 8: End-to-end verification                  ── depends on Step 7 ──
```

**Parallelizable**: Steps 5 and 6 are independent of each other (both depend only on Step 4).

**Recommended Session Flow**:
1. **Session A** — Data Models (Steps 1, 2, 3): Schema wiring + graph model + layout — all pure data with tests
2. **Session B** — Rendering (Steps 4, 5, 6): Node editor integration + hover + inspect stub
3. **Session C** — Integration + Verification (Steps 7, 8): App wiring + end-to-end testing

---

## 6. Directory Structure After Phase 4

```
include/daedalus/
├── app.hpp                       # Updated: +TopologyGraph, +TopologyView
├── protocol/
│   ├── client.hpp                # Unchanged
│   ├── schema.hpp                # Updated: +WireInfo, +wiring in Schema
│   └── telemetry.hpp             # Unchanged
├── data/
│   ├── signal_buffer.hpp         # Unchanged
│   ├── signal_tree.hpp           # Unchanged
│   └── telemetry_queue.hpp       # Unchanged
└── views/
    ├── plotter.hpp               # Unchanged (Phase 2)
    ├── console.hpp               # Unchanged (Phase 3)
    ├── controls.hpp              # Unchanged (Phase 3)
    ├── inspector.hpp             # Unchanged (Phase 3)
    └── topology.hpp              # NEW: TopologyGraph, TopologyView

src/daedalus/
├── main.cpp                      # Unchanged
├── app.cpp                       # Updated: topology integration, docking
├── protocol/
│   ├── schema.cpp                # Updated: wiring parsing
│   └── client.cpp                # Unchanged
├── data/
│   └── signal_tree.cpp           # Unchanged
└── views/
    ├── plotter.cpp               # Unchanged (Phase 2)
    ├── console.cpp               # Unchanged (Phase 3)
    ├── controls.cpp              # Unchanged (Phase 3)
    ├── inspector.cpp             # Unchanged (Phase 3)
    └── topology.cpp              # NEW: Node editor rendering + layout

tests/
├── test_main.cpp                 # Unchanged
├── protocol/
│   ├── test_telemetry.cpp        # Unchanged
│   ├── test_schema.cpp           # Updated: +wiring parsing tests
│   └── test_client.cpp           # Unchanged
├── data/
│   ├── test_signal_buffer.cpp    # Unchanged
│   ├── test_signal_tree.cpp      # Unchanged
│   └── test_telemetry_queue.cpp  # Unchanged
└── views/
    ├── test_plotter.cpp          # Unchanged (Phase 2)
    ├── test_console.cpp          # Unchanged (Phase 3)
    ├── test_controls.cpp         # Unchanged (Phase 3)
    ├── test_inspector.cpp        # Unchanged (Phase 3)
    └── test_topology.cpp         # NEW: Graph construction + layout tests
```

---

## 7. Performance Budget

### Per-Frame Costs (60 Hz, 16.67ms budget)

| Component | Est. Cost | Notes |
|:----------|:----------|:------|
| Queue drain | <0.5 ms | Same as Phase 1-3 |
| Frame decode + buffer push | <0.5 ms | Same as Phase 1-3 |
| Signal tree render | <1 ms | Same as Phase 1-3 |
| Plot rendering | ~3-5 ms | Same as Phase 2-3 |
| Playback controls | <0.1 ms | Same as Phase 3 |
| Console render | <0.5 ms | Same as Phase 3 |
| **Topology rendering** | **~1-2 ms** | Node editor with <20 nodes, <50 links |
| **Hover tooltips** | **<0.1 ms** | Conditional on hover, single tooltip |
| **Flow animation** | **~0.2 ms** | Built into imgui-node-editor |
| ImGui internals | ~5-8 ms | Widget rendering, text layout |
| **Total** | **~12-17 ms** | Within 16.67ms budget |

### Scaling Notes

- **Node count**: Realistic simulations have 2-20 modules. Even at 100 modules, imgui-node-editor handles rendering efficiently via internal culling.
- **Link count**: Wiring is typically sparse (<<N^2). The icarus_rocket example has ~90 signals but only 1 wire.
- **Graph building**: `build_from_schema()` runs once per schema change (not per frame). For 20 modules with 100 wires, it's <1ms.
- **Layout**: `compute_layout()` runs once, then node positions are managed by the editor. Reset layout re-runs it on demand.
- **Tooltip lookup**: Linear scan over pins for hover. With <100 pins total, this is negligible (<0.01ms).

---

## 8. Testing Strategy

### What We Test (Unit Tests)

| Area | Test Count | Description |
|:-----|:-----------|:------------|
| Schema wiring parsing | ~6 | Parse wiring, defaults, malformed, empty |
| TopologyGraph building | ~12 | Node/pin/link construction, ID scheme, dedup, clear |
| Auto-layout | ~7 | Layer assignment, positioning, edge cases |

**Total new tests**: ~25 cases
**Running total**: ~77 (Phase 1-3) + ~25 = ~102 tests

### What We Don't Test (Manual Verification)

- imgui-node-editor rendering output
- Node dragging interaction
- Flow animation appearance
- Hover tooltip positioning
- Docking tab switching

### What We Verify End-to-End (Step 8)

Full interactive testing against live Hermes data:
- Topology renders correctly from multi-module config
- Auto-layout positions nodes in readable arrangement
- Live values appear in hover tooltips
- Flow animation indicates data streaming
- Inspect command sends correctly (error from Hermes expected)
- All Phase 1-3 features remain functional

---

## 9. Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| imgui-node-editor API mismatch with ImGui Bundle version | Build failure | Low | API is stable (v0.9-0.10). Bundle includes compatible fork. Test early in Step 4. |
| Node editor context conflicts with ImPlot context | Rendering glitches | Low | Each has separate context management. Set/restore current editor in render scope. |
| `ed::Suspend()`/`Resume()` not called correctly | Tooltip rendering breaks | Medium | Wrap all non-editor ImGui calls (tooltips, popups) in Suspend/Resume. Test this first. |
| Large module nodes (90+ signals) clutter the view | Unusable layout | Low | Show only wired signals as pins, collapse others to count. Mitigated by design (Section 3.4). |
| No wiring in simple configs | Empty topology view | Expected | Display informative message. The view is useful for multi-module configs. |
| Node editor `SettingsFile` writes to disk | Unexpected file | Low | Set `SettingsFile = nullptr` (or empty string) in Config. |
| Layout positions overridden by editor save/restore | Unexpected node positions | Medium | Use `SettingsFile = nullptr` and `layout_dirty_` flag pattern. |
| Inspect command rejected by Hermes | User confusion | Expected | Menu item labeled "(requires Hermes support)". Error appears in console log. |
| Tab switching between Plots and Topology loses state | ImPlot/node-editor state loss | Low | Each view has persistent state. HelloImGui docking preserves widget state for inactive tabs. |

---

## 10. Phase 4 Definition of Done

- [x] `WireInfo` struct added to schema types
- [x] `parse_schema()` extracts optional wiring array
- [x] ~6 new schema parsing tests for wiring
- [x] `TopologyGraph` builds nodes, pins, and links from schema
- [x] Graph correctly identifies wired vs. unwired signals
- [x] ~12 new graph construction tests
- [x] Auto-layout positions nodes in left-to-right layers
- [x] ~7 new layout algorithm tests
- [x] `TopologyView` renders module nodes with imgui-node-editor
- [x] Output pins on source signals, input pins on destination signals
- [x] Links drawn between wired pins with flow animation
- [x] Nodes are draggable (built into editor)
- [x] "Reset Layout" button re-applies auto-layout
- [x] "Fit to View" button navigates to content
- [x] Hover on pin shows signal path and current value
- [x] Hover on link shows wire details (src→dst, value, gain, offset)
- [x] Values update in real-time from signal buffers
- [x] Right-click module node shows context menu with "Inspect" option
- [x] Inspect sends command to Hermes (error expected, displayed in console)
- [x] Topology view tabbed with Plots in main dock space
- [x] No-wiring schema shows informative message (not empty/broken)
- [x] Reconnection rebuilds topology from new schema
- [x] All Phase 1-3 tests still pass (no regressions)
- [x] ~25 new unit tests for Phase 4
- [x] `./scripts/ci.sh` passes (clean build + all tests)
- [x] End-to-end verification against `multi_module.yaml` (wiring)
- [x] End-to-end verification against `websocket_telemetry.yaml` (no wiring)

---

## 11. Future Work (Phase 4+)

When Hermes adds the `inspect` protocol action:

1. **Handle inspect ack**: Parse execution path and intermediate values from response
2. **Highlight execution path**: Color links in the execution path differently (e.g., orange highlight)
3. **Show intermediate values**: Display values at each node along the execution path
4. **Subgraph extraction**: Optionally filter the topology to show only the relevant subgraph
5. **Shadow execution animation**: Use `ed::Flow()` with a different color to show the inspect execution trace

These features build directly on the Phase 4 architecture — the `TopologyGraph`, `TopologyView`, and context menu infrastructure are already in place.
