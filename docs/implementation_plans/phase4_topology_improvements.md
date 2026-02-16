# Phase 4 Topology Improvements (v2)

> **Created**: 2026-02-16
> **Revised**: 2026-02-16
> **Branch**: `phase4`
> **Status**: Planning
> **Context**: Post-implementation critique of the topology view. First attempt (Codex) revealed bugs with node movement, sizing, and over-scoped signal expansion.

---

## 1. Problem Statement

The initial topology implementation is functionally correct — nodes render, wires connect, flow animates, hover tooltips work. But the view is **not useful enough to justify a dedicated tab**. With `icarus_rocket.yaml` (92 signals, 1 wire), the user sees two boxes and a line. There is no reason to return to this tab after the first glance.

The core issue: the topology view was designed around **wiring visualization**, but real simulation configs have **sparse wiring** relative to their signal count. The view needs to provide value even when wiring is minimal.

---

## 2. Scope Decisions

### REMOVED: Improvement A (Grouped Collapsible Signals) — DEFERRED

**Reason**: First attempt showed that embedding collapsible signal trees inside nodes bloats box sizes unacceptably. A module with 92 signals produces a node that dominates the viewport and defeats the purpose of a topology *overview*.

**Future plan**: A dedicated "introspection" mode will allow users to double-click a node to expand it into a full signal view. This is a separate feature, not part of topology polish.

### REMOVED: Improvement B (Cross-View Navigation) — DEFERRED

**Reason**: Depended on Improvement A for grouped signal rows. Without embedded signals, there's nothing to double-click. Will be revisited alongside the introspection feature.

### KEPT: C (Pin Rendering), D (Node Headers), E (Wire Labels), F (Node Hover)

These four improvements are independent, incremental, and address real visual clarity problems.

---

## 3. Data Model Changes

Add `total_signal_count` to `TopologyNode`. This is the total number of signals in the module's schema entry (wired + unwired), used for the header badge in Improvement D and the hover summary in Improvement F.

### Header change (`topology.hpp`)

```cpp
struct TopologyNode {
    uintptr_t id = 0;
    std::string module_name;
    ImVec2 position = {0.0f, 0.0f};
    std::vector<TopologyPin> input_pins;
    std::vector<TopologyPin> output_pins;
    size_t unwired_signal_count = 0;
    size_t total_signal_count = 0;      // ← NEW: total signals from schema
};
```

### Set it in `build_from_schema` (`topology.cpp`)

In the existing loop that computes `unwired_signal_count` (lines 136-144), add one line:

```cpp
for (size_t i = 0; i < schema.modules.size(); ++i) {
    // ... existing wired_count computation ...
    nodes_[i].unwired_signal_count = schema.modules[i].signals.size() - wired_count;
    nodes_[i].total_signal_count = schema.modules[i].signals.size();  // ← NEW
}
```

### Test

```cpp
TEST(TopologyGraph, TotalSignalCountReflectsSchemaSize) {
    Schema schema;
    schema.modules.push_back(make_module("inputs", {"thrust_cmd", "pitch_cmd"}));
    schema.modules.push_back(make_module("physics", {"input", "output", "state"}));
    schema.wiring.push_back(WireInfo{"inputs.thrust_cmd", "physics.input", 1.0, 0.0});

    TopologyGraph graph;
    graph.build_from_schema(schema);

    const TopologyNode *inputs = find_node(graph, "inputs");
    const TopologyNode *physics = find_node(graph, "physics");
    ASSERT_NE(inputs, nullptr);
    ASSERT_NE(physics, nullptr);
    EXPECT_EQ(inputs->total_signal_count, 2u);
    EXPECT_EQ(physics->total_signal_count, 3u);
}
```

---

## 4. Improvement C: Pin Rendering Clarity

**Priority**: High — visual polish that reduces confusion

**Current**: `throttle -> 0` / `-> Rocket.Engine.throttle_cmd 0`

**Problems**:
- Text arrows `->` blend with signal names
- No visual distinction between direction, name, and value
- Value looks like part of the name

**New format**:

```
● throttle_cmd              0.85 N
```

`[colored_bullet] [name]  ...spacing...  [value] [unit]`

- Colored bullet communicates direction (blue=input, green=output)
- No text arrows
- Value + unit right-aligned and dimmed
- Direction further communicated by pin placement (left edge vs right edge — already handled by node editor)

### Implementation

**CRITICAL**: All layout inside `BeginPin`/`EndPin` and `BeginNode`/`EndNode` uses *relative* ImGui cursor positions, which the node editor transforms to screen space automatically. This means standard `ImGui::SameLine()` calls work correctly regardless of node position, zoom level, or user dragging. **Do NOT mix canvas coordinates with screen coordinates inside node content.**

**Replace `render_pin` entirely** (`topology.cpp`):

```cpp
void TopologyView::render_pin(const TopologyPin &pin,
                              const std::map<size_t, data::SignalBuffer> &buffers) {
    // Pin region (icon + name) — this is what node-editor tracks for link connections
    ax::NodeEditor::BeginPin(ax::NodeEditor::PinId(pin.id), pin.kind);

    // Colored bullet for direction
    const ImVec4 pin_color = (pin.kind == ax::NodeEditor::PinKind::Input)
                                 ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f)   // Blue input
                                 : ImVec4(0.4f, 1.0f, 0.4f, 1.0f);  // Green output
    ImGui::PushStyleColor(ImGuiCol_Text, pin_color);
    ImGui::Bullet();
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextUnformatted(pin.signal_name.c_str());

    ax::NodeEditor::EndPin();

    // Value + unit AFTER EndPin, on the same line
    // This is still inside BeginNode/EndNode, so layout is relative and robust.
    if (pin.signal_index.has_value()) {
        const auto it = buffers.find(pin.signal_index.value());
        if (it != buffers.end() && !it->second.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("  %.4g", it->second.last_value());
            if (pin.unit.has_value()) {
                ImGui::SameLine(0.0f, 2.0f);
                ImGui::TextDisabled("%s", pin.unit->c_str());
            }
        }
    }
}
```

**Key robustness points**:
- `ImGui::Bullet()` renders a small filled circle using `ImGuiCol_Text` — we override that color per-pin
- Value is rendered *after* `EndPin()` but still on the same line via `SameLine()`
- No fixed pixel offsets — layout adapts to content width
- Node editor auto-sizes the node to fit the widest line
- `SameLine(0.0f, 2.0f)` adds 2px spacing between value and unit (tighter than default)

---

## 5. Improvement D: Node Header Styling

**Priority**: High — visual hierarchy

**Current**: Module name in yellow text, same for all modules. No size cue.

**New**: Color-coded module name with signal count badge.

```
┌──────────────────────────────────────┐
│ rocket                        92 sig │
├──────────────────────────────────────┤
│ ...                                  │
```

### Implementation

The header is rendered using standard ImGui calls inside `BeginNode`/`EndNode`. The colored header *background bar* is drawn using `GetNodeBackgroundDrawList()` **after** `EndNode()`, using screen-space rectangles captured during rendering. This ensures the background tracks node movement correctly.

**Replace `render_node`** (`topology.cpp`):

```cpp
// ---- New helper (in anonymous namespace at top of topology.cpp) ----

// Deterministic color from module name — consistent across frames
ImVec4 module_header_color(const std::string &name) {
    // djb2 hash → hue in [0, 360), fixed saturation/value for readability
    uint32_t hash = 5381;
    for (char c : name) {
        hash = ((hash << 5) + hash) + static_cast<uint32_t>(c);
    }
    const float hue = static_cast<float>(hash % 360) / 360.0f;
    ImVec4 color;
    ImGui::ColorConvertHSVtoRGB(hue, 0.5f, 0.9f, color.x, color.y, color.z);
    color.w = 1.0f;
    return color;
}

// ---- Updated render_node ----

void TopologyView::render_node(const TopologyNode &node,
                               const std::map<size_t, data::SignalBuffer> &buffers) {
    ax::NodeEditor::BeginNode(ax::NodeEditor::NodeId(node.id));

    // --- Header group: captures screen-space bounds for background rect ---
    ImGui::BeginGroup();

    // Module name in per-module color
    const ImVec4 header_color = module_header_color(node.module_name);
    ImGui::PushStyleColor(ImGuiCol_Text, header_color);
    ImGui::TextUnformatted(node.module_name.c_str());
    ImGui::PopStyleColor();

    // Signal count badge — on the same line, right side, dimmed
    ImGui::SameLine();
    ImGui::TextDisabled("  %zu sig", node.total_signal_count);

    ImGui::EndGroup();

    // Capture header bounds in screen space (valid because we're inside a node)
    const ImVec2 header_min = ImGui::GetItemRectMin();
    const ImVec2 header_max = ImGui::GetItemRectMax();

    ImGui::Separator();

    // --- Pins ---
    for (const auto &pin : node.input_pins) {
        render_pin(pin, buffers);
    }
    for (const auto &pin : node.output_pins) {
        render_pin(pin, buffers);
    }

    if (node.unwired_signal_count > 0) {
        ImGui::TextDisabled("... %zu more", node.unwired_signal_count);
    }

    ax::NodeEditor::EndNode();

    // --- Draw header background AFTER EndNode ---
    // GetNodeBackgroundDrawList draws behind node content, in screen coordinates.
    // header_min/header_max were captured in screen space during this frame,
    // so they are always correct regardless of node position or zoom.
    auto *draw_list = ax::NodeEditor::GetNodeBackgroundDrawList(
        ax::NodeEditor::NodeId(node.id));
    if (draw_list != nullptr) {
        constexpr float kPadding = 4.0f;
        const ImVec2 bg_min = {header_min.x - kPadding, header_min.y - kPadding};
        const ImVec2 bg_max = {header_max.x + kPadding, header_max.y + kPadding};
        const ImU32 bg_color = ImGui::ColorConvertFloat4ToU32(
            ImVec4(header_color.x, header_color.y, header_color.z, 0.15f));
        constexpr float kRounding = 4.0f;
        draw_list->AddRectFilled(bg_min, bg_max, bg_color, kRounding,
                                 ImDrawFlags_RoundCornersTop);
    }
}
```

**Why this is robust to node movement**:
- `ImGui::GetItemRectMin()`/`GetItemRectMax()` return **screen-space** coordinates of the header group, computed *this frame*
- `GetNodeBackgroundDrawList()` also operates in **screen space**
- Both are captured/used in the same frame, so they're always in sync
- No stored positions from previous frames, no canvas-to-screen conversion needed
- The node editor handles all panning/zooming transforms internally

**Why the previous attempt broke**: Likely mixed `GetNodePosition()` (canvas coords) with `ImGui::GetItemRect*()` (screen coords), or cached positions across frames. This approach avoids both pitfalls.

---

## 6. Improvement E: Wire Labels for Non-Unity Transforms

**Priority**: Medium — relevant for configs with gain/offset unit conversions

**Current**: Gain/offset only visible in hover tooltip.

**New**: Transform annotation rendered at wire midpoint:

```
inputs ──[ ×0.0175 ]──→ physics
```

### Implementation

Wire labels are rendered **after** all `Link()` calls, using `Suspend()`/`Resume()` to temporarily exit the node editor's coordinate system and draw in screen space.

**Key API calls**:
- `GetNodePosition(nodeId)` → canvas coordinates of node's top-left
- `GetNodeSize(nodeId)` → canvas-space size of node
- `CanvasToScreen(canvasPos)` → convert to screen coordinates for ImGui rendering

**Add new private method** (`topology.hpp`):

```cpp
// In TopologyView private section:
void render_wire_labels(const TopologyGraph &graph,
                        const std::map<size_t, data::SignalBuffer> &buffers);
```

**Implementation** (`topology.cpp`):

```cpp
void TopologyView::render_wire_labels(const TopologyGraph &graph,
                                      const std::map<size_t, data::SignalBuffer> &/*buffers*/) {
    namespace ed = ax::NodeEditor;

    // Build pin_id → node_id lookup
    // We need this to find which node owns each pin, so we can compute
    // the approximate screen position of the wire endpoints.
    std::unordered_map<uintptr_t, uintptr_t> pin_to_node;
    for (const auto &node : graph.nodes()) {
        for (const auto &pin : node.input_pins) {
            pin_to_node[pin.id] = node.id;
        }
        for (const auto &pin : node.output_pins) {
            pin_to_node[pin.id] = node.id;
        }
    }

    for (const auto &link : graph.links()) {
        // Skip unity transforms
        if (link.gain == 1.0 && link.offset == 0.0) {
            continue;
        }

        // Find source and dest nodes
        const auto src_it = pin_to_node.find(link.source_pin_id);
        const auto dst_it = pin_to_node.find(link.dest_pin_id);
        if (src_it == pin_to_node.end() || dst_it == pin_to_node.end()) {
            continue;
        }

        // Source pin is on the RIGHT edge of its node (output)
        // Dest pin is on the LEFT edge of its node (input)
        const ed::NodeId src_node_id(src_it->second);
        const ed::NodeId dst_node_id(dst_it->second);

        const ImVec2 src_pos = ed::GetNodePosition(src_node_id);
        const ImVec2 src_size = ed::GetNodeSize(src_node_id);
        const ImVec2 dst_pos = ed::GetNodePosition(dst_node_id);
        const ImVec2 dst_size = ed::GetNodeSize(dst_node_id);

        // Approximate wire endpoints in canvas space
        const ImVec2 src_point = {src_pos.x + src_size.x, src_pos.y + src_size.y * 0.5f};
        const ImVec2 dst_point = {dst_pos.x, dst_pos.y + dst_size.y * 0.5f};

        // Midpoint in canvas space → convert to screen space
        const ImVec2 midpoint_canvas = {
            (src_point.x + dst_point.x) * 0.5f,
            (src_point.y + dst_point.y) * 0.5f,
        };
        const ImVec2 midpoint_screen = ed::CanvasToScreen(midpoint_canvas);

        // Format the label
        char label[64];
        if (link.offset != 0.0) {
            std::snprintf(label, sizeof(label), u8"×%.4g %+.4g", link.gain, link.offset);
        } else {
            std::snprintf(label, sizeof(label), u8"×%.4g", link.gain);
        }

        // Measure label to center it on the midpoint
        const ImVec2 text_size = ImGui::CalcTextSize(label);

        // Render in screen space (Suspend exits node-editor coordinate system)
        ed::Suspend();
        ImGui::SetCursorScreenPos({
            midpoint_screen.x - text_size.x * 0.5f,
            midpoint_screen.y - text_size.y * 0.5f,
        });
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.3f, 0.9f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        ed::Resume();
    }
}
```

**Call site** — in `render()`, after `render_links()` (line 327):

```cpp
render_links(graph, buffers);
render_wire_labels(graph, buffers);   // ← NEW
handle_hover_tooltips(graph, buffers);
```

**Why this is robust**:
- `GetNodePosition()` + `GetNodeSize()` return current-frame canvas coordinates
- `CanvasToScreen()` handles zoom/pan correctly
- `Suspend()` / `Resume()` bracket lets us use `ImGui::SetCursorScreenPos()` in screen space
- Label is centered on the midpoint using `CalcTextSize()`
- No stored state from previous frames

**Edge case**: If nodes are very close together, the label may overlap the node. This is acceptable — the hover tooltip already shows the same info for overlapping cases.

---

## 7. Improvement F: Node Hover Summary

**Priority**: Low — nice-to-have context

Hovering over a node (not a pin or link) shows a summary tooltip:

```
┌──────────────────────────────┐
│ Module: rocket               │
│ Signals: 92 (1 wired)       │
└──────────────────────────────┘
```

### Implementation

**Add to `handle_hover_tooltips`** (`topology.cpp`), after the existing pin hover block:

```cpp
// --- Node hover tooltip ---
const auto hovered_node = ax::NodeEditor::GetHoveredNode();
if (hovered_node) {
    // Only show node tooltip if no pin or link is hovered (avoid tooltip stacking)
    if (!hovered_pin && !hovered_link) {
        for (const auto &node : graph.nodes()) {
            if (node.id != hovered_node.Get()) {
                continue;
            }

            const size_t wired_count =
                node.input_pins.size() + node.output_pins.size();

            ax::NodeEditor::Suspend();
            ImGui::BeginTooltip();
            ImGui::Text("Module: %s", node.module_name.c_str());
            ImGui::Text("Signals: %zu (%zu wired)",
                         node.total_signal_count, wired_count);
            ImGui::EndTooltip();
            ax::NodeEditor::Resume();
            break;
        }
    }
}
```

**Important**: Check `GetHoveredNode()` *after* checking `GetHoveredPin()` and `GetHoveredLink()`, and only show the node tooltip when neither pin nor link is hovered. This prevents tooltip stacking when the mouse is over a pin inside a node.

**Refactor note**: The existing `handle_hover_tooltips` checks `hovered_link` and `hovered_pin` as local variables. To add the node hover guard, promote them slightly:

```cpp
void TopologyView::handle_hover_tooltips(const TopologyGraph &graph,
                                         const std::map<size_t, data::SignalBuffer> &buffers) {
    // ... existing find_pin_by_id lambda ...

    const auto hovered_link = ax::NodeEditor::GetHoveredLink();
    const auto hovered_pin = ax::NodeEditor::GetHoveredPin();

    // --- Link tooltip (existing code, unchanged) ---
    if (hovered_link) {
        // ... existing link tooltip code ...
    }

    // --- Pin tooltip (existing code, unchanged) ---
    if (hovered_pin) {
        // ... existing pin tooltip code ...
    }

    // --- Node tooltip (NEW) ---
    if (!hovered_pin && !hovered_link) {
        const auto hovered_node = ax::NodeEditor::GetHoveredNode();
        if (hovered_node) {
            for (const auto &node : graph.nodes()) {
                if (node.id != hovered_node.Get()) {
                    continue;
                }
                const size_t wired_count =
                    node.input_pins.size() + node.output_pins.size();

                ax::NodeEditor::Suspend();
                ImGui::BeginTooltip();
                ImGui::Text("Module: %s", node.module_name.c_str());
                ImGui::Text("Signals: %zu (%zu wired)",
                             node.total_signal_count, wired_count);
                ImGui::EndTooltip();
                ax::NodeEditor::Resume();
                break;
            }
        }
    }
}
```

---

## 8. Implementation Order

All four improvements are independent. They can be done in any order or in parallel.

```
C: Pin rendering clarity       ── replace render_pin, no data model change ──
D: Node header styling         ── replace render_node, add total_signal_count ──
E: Wire labels                 ── new render_wire_labels method + call site ──
F: Node hover summary          ── extend handle_hover_tooltips ──
```

**Recommended single-session flow** (all four are small):
1. Data model: add `total_signal_count` to `TopologyNode` + set in `build_from_schema` + test
2. C: Replace `render_pin`
3. D: Replace `render_node`
4. E: Add `render_wire_labels`
5. F: Extend `handle_hover_tooltips`
6. Run `./scripts/ci.sh`

---

## 9. Modified Files

| File | Change |
|:-----|:-------|
| `include/daedalus/views/topology.hpp` | Add `total_signal_count` to `TopologyNode`. Add `render_wire_labels` private method. |
| `src/daedalus/views/topology.cpp` | Set `total_signal_count` in `build_from_schema`. Replace `render_pin` (C). Replace `render_node` (D). New `render_wire_labels` (E). Extend `handle_hover_tooltips` (F). Add `module_header_color` helper. |
| `tests/views/test_topology.cpp` | Add `TotalSignalCountReflectsSchemaSize` test. |

No new files. All changes extend existing Phase 4 code.

---

## 10. Coordinate System Cheat Sheet (For Implementers)

This is the #1 source of bugs in imgui-node-editor rendering. Memorize this:

| Context | Coordinate System | Use For |
|:--------|:------------------|:--------|
| Inside `BeginNode`/`EndNode` | **Relative ImGui** (auto-transformed) | All `ImGui::Text`, `Bullet`, `SameLine`, etc. Just use ImGui normally. |
| `ImGui::GetItemRectMin/Max()` inside a node | **Screen space** | Capturing bounds for later `AddRectFilled` calls. |
| `GetNodePosition(id)` | **Canvas space** | Computing wire midpoints, layout math. |
| `GetNodeSize(id)` | **Canvas space** (dimensions) | Computing wire midpoints, layout math. |
| `CanvasToScreen(pos)` | Canvas → **Screen** | Converting canvas positions for `SetCursorScreenPos` or draw list calls. |
| `GetNodeBackgroundDrawList(id)` | **Screen space** | Drawing behind node content. Pair with `GetItemRectMin/Max`. |
| Inside `Suspend()`/`Resume()` | **Screen space** (normal ImGui) | Tooltips, overlays, wire labels. Use `SetCursorScreenPos` with screen-space values. |

**Golden rule**: Never mix canvas and screen coordinates. If you got a position from `GetNodePosition()`, convert with `CanvasToScreen()` before passing to `SetCursorScreenPos()` or draw list methods.

---

## 11. Definition of Done

- [ ] `total_signal_count` field on `TopologyNode`, set from schema, tested
- [ ] Pin rendering: colored bullet + name + dimmed value + unit (no text arrows)
- [ ] Input pins blue bullet, output pins green bullet
- [ ] Node headers: per-module color + signal count badge + subtle background bar
- [ ] Header background tracks node position correctly when dragged/zoomed
- [ ] Non-unity gain/offset displayed as centered label on wire midpoint
- [ ] Wire labels track correctly when nodes are dragged
- [ ] Node hover shows module summary tooltip (only when no pin/link hovered)
- [ ] All existing Phase 4 tests still pass
- [ ] `./scripts/ci.sh` passes
