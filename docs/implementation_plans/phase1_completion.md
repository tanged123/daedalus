# Phase 1 Completion Plan: Skeleton + Protocol Client (MVP)

> **Created**: 2026-02-08
> **Branch**: `phase1_cont` (continuation of Phase 1 work)
> **Status**: Planning

---

## 1. Current State Assessment

### 1.1 What's DONE (Infrastructure)

| Component | Status | Notes |
|:----------|:-------|:------|
| Nix flake + dev shell | Complete | Hermes input, llvmPackages_latest stdenv, full dev tooling |
| ImGui Bundle Nix derivation | Complete | Custom mkDerivation with fetchgit+fetchSubmodules, clean cmake config replacement, FREETYPE=OFF, FETCHCONTENT_FULLY_DISCONNECTED |
| CMake build system | Complete | C++20, FetchContent for IXWebSocket, find_package for imgui_bundle/nlohmann_json/GTest |
| Scripts directory (9/9) | Complete | dev.sh, build.sh, test.sh, ci.sh, coverage.sh, clean.sh, generate_docs.sh, install-hooks.sh, bump_version.sh |
| CI/CD workflows (4/4) | Complete | ci.yml, format.yml, docs.yml, coverage.yml — all use Nix+Cachix preamble |
| Pre-commit hook | Complete | .github/hooks/pre-commit runs nix fmt + git add -u |
| Agent rules | Complete | CLAUDE.md → .agent/rules/daedalus.md, AGENTS.md for beads |
| Git config | Complete | .gitignore, .gitattributes (beads merge driver), .clang-format |
| Doxyfile | Complete | Configured for C++ (.hpp, .cpp, .md) |
| Hermes reference submodule | Complete | references/hermes/ with protocol docs + examples |
| Beads issue tracking | Complete | .beads/ initialized |
| README.md | Complete | With badges |

### 1.2 What's DONE (Code - Stubs Only)

| File | Status | Content |
|:-----|:-------|:--------|
| `include/daedalus/app.hpp` | Stub | Class declaration with constructor, destructor, `run()` |
| `src/daedalus/app.cpp` | Stub | Default constructor/destructor, `run()` returns 0 with TODO comments |
| `src/main.cpp` | Complete | Creates App and calls `run()` |
| `tests/test_main.cpp` | Stub | Single placeholder `EXPECT_TRUE(true)` test |

### 1.3 What's MISSING (Phase 1 Deliverables)

These are the unchecked items from the bootstrap guide Section 16:

| # | Deliverable | Priority | Complexity |
|:--|:------------|:---------|:-----------|
| 1 | Binary telemetry decoder (`protocol/telemetry.hpp`) | High | Low |
| 2 | Schema parser (`protocol/schema.hpp`) | High | Medium |
| 3 | Signal tree (`data/signal_tree.hpp`) | High | Medium |
| 4 | SPSC lock-free telemetry queue (`data/telemetry_queue.hpp`) | High | Medium |
| 5 | Signal buffer / ring buffer (`data/signal_buffer.hpp`) | High | Low-Medium |
| 6 | WebSocket client (`protocol/client.hpp`) | High | Medium |
| 7 | App lifecycle with ImGui window | High | Medium |
| 8 | Connection status indicator | Medium | Low |
| 9 | Signal tree browser UI widget | Medium | Medium |
| 10 | End-to-end verification | High | Low |
| 11 | Tests for all data structures | High | Medium |

---

## 2. Implementation Steps

### Step 1: Binary Telemetry Decoder

**Files**: `include/daedalus/protocol/telemetry.hpp`, `tests/protocol/test_telemetry.cpp`

**Why first**: Pure data structure, zero dependencies beyond `<cstdint>` and `<cstring>`. Everything downstream depends on being able to decode binary frames.

**Requirements**:
- `TelemetryHeader` struct (24 bytes, `static_assert` on size)
  - `uint32_t magic` (0x48455254 = "HERT")
  - `uint64_t frame` (monotonic frame counter)
  - `double time` (simulation time in seconds)
  - `uint32_t count` (number of signal values)
- `decode_frame()` function
  - Input: `const uint8_t* data, size_t len`
  - Output: `TelemetryHeader& hdr, std::span<const double>& values`
  - Returns: `bool` (false if magic mismatch, insufficient length, or count mismatch)
  - All fields little-endian (native on x86, which is our target)
- Header-only implementation (no .cpp needed for this small utility)

**Tests** (`tests/protocol/test_telemetry.cpp`):
- Valid frame decoding (4 signals, known values)
- Magic validation failure (wrong magic bytes)
- Truncated header (< 24 bytes)
- Truncated payload (header says N signals but data is short)
- Zero-signal frame (valid edge case)
- Large frame (100+ signals)

**Acceptance**: All tests pass, `decode_frame` correctly parses frames matching the Hermes binary spec.

---

### Step 2: Schema Parser

**Files**: `include/daedalus/protocol/schema.hpp`, `src/daedalus/protocol/schema.cpp`, `tests/protocol/test_schema.cpp`

**Why second**: Needed to build the signal tree and to know what signals are available for subscription.

**Requirements**:
- `SignalInfo` struct: `name` (string), `type` (string, e.g. "f64"), `unit` (optional string)
- `ModuleInfo` struct: `name` (string), `signals` (vector of SignalInfo)
- `Schema` struct: `modules` (vector of ModuleInfo)
- `parse_schema(const nlohmann::json& msg) -> Schema`
  - Input: Full JSON message with `"type": "schema"` and `"modules": {...}`
  - Output: Parsed Schema struct
  - Throws on malformed JSON or missing required fields
- `parse_subscribe_ack(const nlohmann::json& msg) -> SubscribeAck`
  - `SubscribeAck` struct: `count` (uint32_t), `signals` (vector of fully-qualified signal names in order)
  - This order defines binary payload layout

**JSON format** (from Hermes spec):
```json
{
  "type": "schema",
  "modules": {
    "vehicle": {
      "signals": [
        {"name": "position.x", "type": "f64", "unit": "m"},
        {"name": "position.y", "type": "f64", "unit": "m"}
      ]
    }
  }
}
```

**Tests** (`tests/protocol/test_schema.cpp`):
- Parse valid schema with single module
- Parse valid schema with multiple modules
- Signal with and without `unit` field
- Subscribe ack parsing (signal order preservation)
- Empty modules object
- Missing `type` field → exception
- Missing `signals` field → exception

**Acceptance**: Correctly parses schema JSON matching the Hermes spec. Subscribe ack preserves signal order.

---

### Step 3: Signal Tree

**Files**: `include/daedalus/data/signal_tree.hpp`, `src/daedalus/data/signal_tree.cpp`, `tests/data/test_signal_tree.cpp`

**Why third**: Depends on Schema parser. The tree is the user-facing representation of available signals.

**Requirements**:
- Hierarchical tree built from flat signal names using `.` as separator
  - E.g., `vehicle.position.x` → `vehicle` > `position` > `x`
- `SignalTreeNode` struct:
  - `name` (string, segment name)
  - `children` (vector of unique_ptr<SignalTreeNode>, sorted by name)
  - `full_path` (string, fully qualified name like "vehicle.position.x")
  - `is_leaf` (bool, true if this is an actual signal, not just a namespace)
  - `signal_index` (optional<size_t>, index in subscription order, set after subscribe ack)
- `SignalTree` class:
  - `build_from_schema(const Schema& schema)` — builds tree from parsed schema
  - `update_subscription(const SubscribeAck& ack)` — assigns signal_index to leaf nodes
  - `root() -> const SignalTreeNode&` — root of the tree
  - `find(const std::string& path) -> const SignalTreeNode*` — lookup by full path
  - `all_signals() -> std::vector<std::string>` — all leaf signal paths
- Thread safety: render thread only (no synchronization needed)

**Tests** (`tests/data/test_signal_tree.cpp`):
- Build tree from schema with 2 modules, 4 signals
- Node hierarchy is correct (root > module > signal groups > leaves)
- `find()` returns correct nodes
- `all_signals()` returns all leaf paths
- `update_subscription()` assigns correct indices
- Empty schema produces empty tree

**Acceptance**: Tree correctly represents the hierarchical signal namespace. Subscription indices are correctly mapped.

---

### Step 4: Signal Buffer (Ring Buffer)

**Files**: `include/daedalus/data/signal_buffer.hpp`, `tests/data/test_signal_buffer.cpp`

**Why fourth**: Independent data structure. Needed before we can store telemetry history.

**Requirements**:
- Fixed-capacity ring buffer for `double` values
- Default capacity: 18,000 samples (5 minutes at 60 Hz)
- `SignalBuffer` class:
  - Constructor takes capacity (default 18000)
  - `push(double value)` — append value, overwrite oldest if full
  - `push(double time, double value)` — append time+value pair
  - `size() -> size_t` — current number of stored samples
  - `capacity() -> size_t` — maximum capacity
  - `full() -> bool`
  - `clear()`
  - `time_data() -> const double*` — pointer to time array (for ImPlot)
  - `value_data() -> const double*` — pointer to value array (for ImPlot)
  - `operator[](size_t i) -> double` — access by index (0 = oldest)
- Implementation: Two contiguous arrays (times[], values[]) with head/count tracking
  - ImPlot needs contiguous arrays, so use two separate `std::vector<double>` with head pointer
  - When the buffer wraps, data is not contiguous — need to handle this for ImPlot
  - **Strategy**: Use a single contiguous vector and `memmove` on wrap, OR use two-segment rendering with ImPlot's offset parameter. Prefer the simpler approach: just use a circular buffer and provide `data()` + `offset()` for ImPlot's `PlotLine` with stride.
  - Actually, simplest: maintain a **write-position** and expose raw arrays. ImPlot's `PlotLine` can handle non-contiguous via offset parameter. Alternatively, copy to a contiguous staging buffer each frame (18k doubles = 144KB, trivial to copy at 60fps).
- Thread safety: render thread only

**Tests** (`tests/data/test_signal_buffer.cpp`):
- Push values, verify size increases
- Push beyond capacity, verify oldest overwritten
- Clear resets state
- Data access returns correct values in order
- Time+value paired storage
- Boundary: push exactly capacity values
- Boundary: push capacity+1 values

**Acceptance**: Ring buffer correctly stores and retrieves signal history. Data is accessible for ImPlot rendering.

---

### Step 5: SPSC Lock-Free Queue (TelemetryQueue + EventQueue)

**Files**: `include/daedalus/data/telemetry_queue.hpp`, `tests/data/test_telemetry_queue.cpp`

**Why fifth**: Needed for thread-safe data transfer from network thread to render thread.

**Requirements**:
- `SPSCQueue<T>` — generic single-producer single-consumer lock-free ring buffer
  - Template parameter T (will be instantiated with `std::vector<uint8_t>` for binary frames, and `nlohmann::json` or `std::string` for events)
  - Fixed capacity (configurable, default 256 for telemetry, 64 for events)
  - `try_push(T&& item) -> bool` — returns false if full (producer never blocks)
  - `try_pop(T& item) -> bool` — returns false if empty (consumer never blocks)
  - `size_approx() -> size_t` — approximate count (for diagnostics)
  - Lock-free using `std::atomic<size_t>` head/tail with `memory_order_acquire`/`memory_order_release`
- `TelemetryQueue` — type alias or thin wrapper around `SPSCQueue<std::vector<uint8_t>>`
- `EventQueue` — type alias or thin wrapper around `SPSCQueue<std::string>` (raw JSON strings)
  - Using strings rather than parsed JSON to keep parsing on the render thread

**Implementation notes**:
- Standard SPSC ring buffer pattern: power-of-2 capacity, head/tail atomics
- No `std::mutex` anywhere — pure lock-free
- Producer (network thread): `try_push` with `release` store on tail
- Consumer (render thread): `try_pop` with `acquire` load on tail
- Cache-line padding between head and tail to avoid false sharing

**Tests** (`tests/data/test_telemetry_queue.cpp`):
- Single-threaded: push/pop sequence
- Push to full → try_push returns false
- Pop from empty → try_pop returns false
- Push N, pop N, verify order preserved (FIFO)
- Wrap-around: push capacity+1, verify wrap behavior
- Multi-threaded stress test: producer pushes 10k items, consumer pops them, verify all received in order

**Acceptance**: Lock-free SPSC queue passes single-threaded and multi-threaded tests. No data corruption under concurrent access.

---

### Step 6: WebSocket Client (HermesClient)

**Files**: `include/daedalus/protocol/client.hpp`, `src/daedalus/protocol/client.cpp`, `tests/protocol/test_client.cpp`

**Why sixth**: Depends on Steps 1-5 (decoder, schema parser, queues). This is the integration layer.

**Requirements**:
- `HermesClient` class:
  - Constructor: takes URL (default `ws://127.0.0.1:8765`)
  - `connect()` — initiates WebSocket connection via IXWebSocket
  - `disconnect()` — cleanly closes connection
  - `subscribe(const std::vector<std::string>& patterns)` — sends subscribe command
  - `send_command(const std::string& action, const nlohmann::json& params = {})` — generic command sender
  - `pause()`, `resume()`, `reset()`, `step(int count)` — convenience methods
  - `set_signal(const std::string& signal, double value)` — signal injection
  - `telemetry_queue() -> TelemetryQueue&` — access to binary frame queue
  - `event_queue() -> EventQueue&` — access to JSON event queue
  - `state() -> ConnectionState` — current connection state enum
- `ConnectionState` enum: `Disconnected`, `Connecting`, `Connected`, `Error`
- **Threading**: IXWebSocket runs callbacks on its own background thread
  - `onMessage` callback:
    - Text frames → push raw JSON string to EventQueue
    - Binary frames → validate magic, push raw bytes to TelemetryQueue
  - `onOpen/onClose/onError` → push connection event to EventQueue
- **Auto-reconnect**: Use IXWebSocket's built-in auto-reconnect (exponential backoff, 1s→30s)

**Implementation notes**:
- IXWebSocket API: `ix::WebSocket ws; ws.setUrl(...); ws.setOnMessageCallback(...); ws.start();`
- Keep HermesClient lightweight — it's a thin wrapper around IXWebSocket + queues
- No protocol state machine in the client — the render thread handles schema/ack processing

**Tests** (`tests/protocol/test_client.cpp`):
- **Unit tests only** (no actual WebSocket server):
  - Command JSON formatting (subscribe, pause, resume, reset, step, set)
  - Verify command JSON matches Hermes spec exactly
- Integration testing is done manually via end-to-end verification (Step 8)

**Acceptance**: Client correctly formats all Hermes protocol commands. Callbacks correctly route text/binary frames to appropriate queues.

---

### Step 7: App Lifecycle + ImGui Window

**Files**: `include/daedalus/app.hpp` (update), `src/daedalus/app.cpp` (rewrite), `src/main.cpp` (no change)

**Why seventh**: Depends on Steps 3, 5, 6 (signal tree, queues, client). This brings everything together.

**Requirements**:
- `App::run()` lifecycle:
  1. Initialize Hello ImGui runner with `RunnerParams`
  2. Set up docking layout (use Hello ImGui's `DockingParams`)
  3. Create `HermesClient` and connect
  4. Enter render loop (Hello ImGui manages this)
  5. Each frame:
     a. Poll `EventQueue` — handle schema, acks, events, errors
     b. Poll `TelemetryQueue` — decode frames, update `SignalBuffer`s
     c. Render UI windows
  6. On exit: disconnect client, cleanup

- **Windows** (Phase 1 minimal set):
  - **Connection Status Bar**: Shows ConnectionState (connected/connecting/disconnected), server URL, signal count
  - **Signal Tree Browser**: TreeNode hierarchy from SignalTree, expandable/collapsible
    - Leaf nodes show current value (last received)
    - Later phases add drag-and-drop to plotter

- **Hello ImGui setup**:
  - Use `HelloImGui::Run()` with `RunnerParams`
  - Enable docking
  - Set window title "Daedalus"
  - Dark theme (ImGui default dark is fine for Phase 1)

- **Signal processing pipeline** (each frame):
  1. Drain EventQueue:
     - `"schema"` → parse schema, build SignalTree, auto-subscribe to all signals (`"*"`)
     - `"ack"` for subscribe → update SignalTree subscription indices, create SignalBuffers, send `resume`
     - `"event"` → update connection status display
     - `"error"` → log to console (just stdout for Phase 1)
  2. Drain TelemetryQueue:
     - Decode each binary frame via `decode_frame()`
     - For each signal value, push to corresponding SignalBuffer

- **SignalRegistry**: Maps subscription index (size_t) → SignalBuffer*
  - Built when subscribe ack is received
  - Used during telemetry processing to route values to buffers

**Implementation notes**:
- Hello ImGui handles the GLFW/OpenGL initialization
- `RunnerParams::callbacks.ShowGui` is the per-frame callback
- Keep App members: `HermesClient`, `SignalTree`, `SignalRegistry` (map<size_t, SignalBuffer>), connection state

**Acceptance**: App opens a window, connects to Hermes, displays signal tree, shows connection status.

---

### Step 8: End-to-End Verification

**No new files** — this is a manual verification step.

**Procedure**:
1. Start Hermes: `python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml`
2. Run Daedalus: `./scripts/build.sh && ./build/daedalus`
3. Verify:
   - [ ] Window opens with "Daedalus" title
   - [ ] Connection status shows "Connected" (green indicator)
   - [ ] Signal tree shows: `vehicle` → `position` → `x`, `y` and `velocity` → `x`, `y`
   - [ ] Leaf nodes show updating values (numbers changing each frame)
   - [ ] Disconnecting Hermes (Ctrl+C) → status shows "Disconnected" / "Connecting" (auto-reconnect)
   - [ ] Restarting Hermes → Daedalus reconnects, re-subscribes, values resume
4. Run tests: `./scripts/test.sh` — all pass
5. Run CI: `./scripts/ci.sh` — clean build + all tests pass

**Acceptance**: Full end-to-end data flow works: Hermes → WebSocket → Daedalus → signals visible in tree browser.

---

## 3. CMake Updates Required

The current `CMakeLists.txt` needs updates to support the new source files:

```cmake
# Main library — add all implementation files
add_library(daedalus_lib STATIC
  src/daedalus/app.cpp
  src/daedalus/protocol/schema.cpp
  src/daedalus/protocol/client.cpp
  src/daedalus/data/signal_tree.cpp
)

# Tests — add all test files
add_executable(daedalus_tests
  tests/test_main.cpp
  tests/protocol/test_telemetry.cpp
  tests/protocol/test_schema.cpp
  tests/protocol/test_client.cpp
  tests/data/test_signal_buffer.cpp
  tests/data/test_signal_tree.cpp
  tests/data/test_telemetry_queue.cpp
)
```

Header-only components (telemetry decoder, signal buffer, SPSC queue) do not need .cpp files in the library.

---

## 4. Directory Structure After Phase 1

```
include/daedalus/
├── app.hpp                       # Updated: App with HermesClient, SignalTree, render loop
├── protocol/
│   ├── client.hpp                # NEW: HermesClient (IXWebSocket wrapper + queues)
│   ├── schema.hpp                # NEW: Schema/SubscribeAck parsing
│   └── telemetry.hpp             # NEW: TelemetryHeader + decode_frame()
└── data/
    ├── signal_buffer.hpp         # NEW: Per-signal ring buffer (header-only)
    ├── signal_tree.hpp           # NEW: Hierarchical signal tree
    └── telemetry_queue.hpp       # NEW: SPSC lock-free queue (header-only)

src/daedalus/
├── app.cpp                       # Updated: Full lifecycle with Hello ImGui
├── protocol/
│   ├── schema.cpp                # NEW: Schema parsing implementation
│   └── client.cpp                # NEW: HermesClient implementation
└── data/
    └── signal_tree.cpp           # NEW: SignalTree implementation

tests/
├── test_main.cpp                 # Updated: Remove placeholder, keep as GTest entry
├── protocol/
│   ├── test_telemetry.cpp        # NEW: Binary decoder tests
│   ├── test_schema.cpp           # NEW: Schema parser tests
│   └── test_client.cpp           # NEW: Command formatting tests
└── data/
    ├── test_signal_buffer.cpp    # NEW: Ring buffer tests
    ├── test_signal_tree.cpp      # NEW: Signal tree tests
    └── test_telemetry_queue.cpp  # NEW: SPSC queue tests
```

---

## 5. Dependency Graph

```
Step 1: telemetry.hpp (decoder)         ── no deps ──
Step 2: schema.hpp (parser)             ── depends on nlohmann_json ──
Step 3: signal_tree.hpp                 ── depends on Step 2 (Schema types) ──
Step 4: signal_buffer.hpp               ── no deps ──
Step 5: telemetry_queue.hpp (SPSC)      ── no deps ──
Step 6: client.hpp (HermesClient)       ── depends on Steps 1, 2, 5 ──
Step 7: app.cpp (lifecycle + UI)        ── depends on Steps 3, 4, 5, 6 ──
Step 8: End-to-end verification         ── depends on Step 7 ──
```

**Parallelizable**: Steps 1, 2, 4, 5 can all be implemented in parallel (no inter-dependencies). Step 3 depends on Step 2. Step 6 depends on 1, 2, 5. Step 7 depends on everything.

---

## 6. Risks and Mitigations

| Risk | Impact | Mitigation |
|:-----|:-------|:-----------|
| ImGui Bundle cmake config doesn't work in dev shell | Blocks all UI work | Already solved — custom cmake config in flake.nix postInstall. Verify with `cmake -B build -G Ninja` |
| IXWebSocket FetchContent fails in Nix sandbox | Blocks package build | `FETCHCONTENT_FULLY_DISCONNECTED=ON` in Nix package. Dev shell builds fetch normally |
| Hello ImGui API differences from docs | Slows Step 7 | ImGui Bundle includes Hello ImGui demos. Check `external/hello_imgui/` for API |
| SPSC queue correctness under concurrency | Data corruption | Stress test with producer/consumer threads. Use standard acquire/release pattern |
| ImPlot non-contiguous ring buffer rendering | Visual glitches | Use staging buffer copy (144KB at 60fps is negligible) or ImPlot offset parameter |
| No display server in CI (headless) | Tests that touch ImGui fail in CI | Keep ImGui/rendering out of unit tests. Only test data structures and protocol parsing |

---

## 7. Testing Strategy

### What We Test (Unit Tests)
- **Protocol**: telemetry decoding, schema parsing, command JSON formatting
- **Data Structures**: ring buffer push/pop/wrap, signal tree construction, SPSC queue thread safety

### What We Don't Test (Manual Verification)
- ImGui rendering output
- Actual WebSocket connectivity (requires live Hermes)
- Window appearance and layout

### Test Organization
```
tests/
├── test_main.cpp                 # GTest entry point (can be minimal)
├── protocol/
│   ├── test_telemetry.cpp        # ~6 test cases
│   ├── test_schema.cpp           # ~7 test cases
│   └── test_client.cpp           # ~5 test cases
└── data/
    ├── test_signal_buffer.cpp    # ~7 test cases
    ├── test_signal_tree.cpp      # ~6 test cases
    └── test_telemetry_queue.cpp  # ~6 test cases (incl. multi-threaded)
```

Total: ~37 test cases covering all non-UI Phase 1 components.

---

## 8. Estimated Implementation Order (Recommended Session Flow)

1. **Session A** — Foundation (Steps 1, 2, 4, 5 in parallel):
   - Implement telemetry decoder + tests
   - Implement schema parser + tests
   - Implement signal buffer + tests
   - Implement SPSC queue + tests
   - Update CMakeLists.txt for new files

2. **Session B** — Integration (Steps 3, 6):
   - Implement signal tree + tests (depends on schema types)
   - Implement HermesClient + tests (depends on decoder, schema, queue)

3. **Session C** — UI + Verification (Steps 7, 8):
   - Implement App lifecycle with Hello ImGui
   - Connection status bar
   - Signal tree browser widget
   - End-to-end verification against live Hermes

---

## 9. Phase 1 Definition of Done

- [x] All 8 steps complete
- [x] `./scripts/ci.sh` passes (clean release build + all tests)
- [x] `./scripts/test.sh` passes (all ~37 test cases)
- [x] End-to-end verification checklist passes (Section Step 8)
- [x] No compiler warnings with `-Wall -Wextra`
- [x] Code formatted with `nix fmt`
- [x] All Phase 1 items in bootstrap guide Section 16 checked off
