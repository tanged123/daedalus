# Daedalus Repository Bootstrap Guide

> **Purpose**: This document provides comprehensive instructions for bootstrapping the **Daedalus** Mission Control Visualization Suite repository. Daedalus is the visualization layer of the Project Icarus ecosystem, connecting to Hermes via WebSocket to render real-time simulation telemetry.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Architectural Context](#2-architectural-context)
3. [Pantheon Conventions](#3-pantheon-conventions)
4. [Repository Structure](#4-repository-structure)
5. [Nix Setup](#5-nix-setup)
6. [CMake Configuration](#6-cmake-configuration)
7. [Scripts Directory](#7-scripts-directory)
8. [Technology Stack](#8-technology-stack)
9. [Hermes Protocol Reference](#9-hermes-protocol-reference)
10. [Data Pipeline & Threading Model](#10-data-pipeline--threading-model)
11. [Development Workflow](#11-development-workflow)
12. [Testing Framework](#12-testing-framework)
13. [CI/CD Workflows](#13-cicd-workflows)
14. [Agent Rules](#14-agent-rules)
15. [Git Configuration](#15-git-configuration)
16. [Implementation Roadmap](#16-implementation-roadmap)
17. [Verification Checklist](#17-verification-checklist)

---

## 1. Project Overview

### What is Daedalus?

**Daedalus** is a high-density Mission Control visualization suite for aerospace simulation. Named after the mythological craftsman who built the labyrinth and fashioned wings for his son Icarus, Daedalus provides the tools to observe and understand simulation behavior.

Daedalus provides:

- **Signal Plotter**: Drag-and-drop signal visualization using ImPlot
- **3D World View**: Vehicle position/attitude rendered on Earth (future phase)
- **Topology View**: Module wiring diagrams via imgui-node-editor
- **Console/Log View**: Event stream, phase transitions, command history
- **Inspect Mode**: Shadow execution subgraph rendering for debugging

### Design Principles

> **"Daedalus knows nothing about Icarus."**

Daedalus is a generic visualization client for the Hermes protocol. It has no knowledge of physics, simulation internals, or specific module implementations. All data arrives through the Hermes WebSocket protocol as generic signals.

- **Protocol-Driven**: Everything comes from Hermes via WebSocket
- **Density-First**: Maximize information per pixel, mission control aesthetic
- **Portable**: Desktop (native C++) first, with future browser (WASM) target

### The Icarus Ecosystem

```
┌─────────────────────────────────────────────────────────────┐
│                    DAEDALUS (Visualizer)            ← You   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ 3D World │  │ Plotting │  │ Topology │  │ Console  │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
└────────────────────────┬────────────────────────────────────┘
                         │ WebSocket (Hermes Protocol)
┌────────────────────────┴────────────────────────────────────┐
│                    HERMES (STEP)                              │
│  Signal Bus → Module Adapters → Scheduler                    │
└────────────────────────┬────────────────────────────────────┘
                         │ C API (cffi)
┌────────────────────────┴────────────────────────────────────┐
│                    ICARUS (Physics Core)                      │
│  6DOF Engine → Components → Signal Backplane                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Architectural Context

Daedalus communicates with Hermes over a single WebSocket connection that carries two types of frames:

| Frame Type | Transport | Content |
|:-----------|:----------|:--------|
| **Control** | WebSocket Text (JSON) | Schema, commands, acks, events, errors |
| **Telemetry** | WebSocket Binary | Signal values at configured rate |

### Connection Lifecycle

1. Client connects to `ws://host:port` (default `ws://127.0.0.1:8765`)
2. Server sends `schema` message (all modules and their signals)
3. Client sends `subscribe` command for signals of interest
4. Server acknowledges with `ack` (includes resolved signal list and count)
5. Client sends `resume` to start telemetry flow
6. Server streams binary telemetry frames at configured rate (default 60 Hz)
7. Client can send control commands at any time (pause, resume, reset, step, set)
8. Server sends events asynchronously (state changes, errors)

### Reconnection & Error Handling

The connection may drop due to simulation restart, network interruption, or server shutdown. Daedalus must handle:

- **Graceful close** (WebSocket close code 1001 "going away"): Simulation ended normally
- **Unexpected disconnect**: Auto-reconnect with exponential backoff (1s, 2s, 4s, max 30s)
- **State reset on reconnect**: Clear all signal buffers; re-subscribe after receiving new schema
- **UI feedback**: Connection status indicator visible at all times (connected / connecting / disconnected)

### What Daedalus Does NOT Do

- Parse physics equations
- Run simulation steps
- Access shared memory or IPC
- Know about Icarus components or signal semantics

---

## 3. Pantheon Conventions

All repositories in the Pantheon (Janus, Vulcan, Icarus, Hermes, Daedalus) share a common infrastructure pattern. This section documents what is shared and what is project-specific.

### Shared Across All Repos

| Convention | Details |
|:-----------|:--------|
| **Nix** | `flake.nix` + `flake.lock` for reproducible builds |
| **Cachix** | `tanged123.cachix.org` binary cache |
| **treefmt** | `nix fmt` for auto-formatting (via treefmt-nix) |
| **Pre-commit hook** | `.github/hooks/pre-commit` runs `nix fmt && git add -u` |
| **Scripts** | `scripts/` directory with standardized names |
| **GitHub Actions** | 4 workflows: `ci.yml`, `format.yml`, `docs.yml`, `coverage.yml` |
| **Doxygen** | `Doxyfile` at repo root, output to `build/docs/html` |
| **Beads** | `bd` issue tracking, `.beads/`, `.gitattributes` merge driver |
| **Agent rules** | `CLAUDE.md` → `.agent/rules/{project}.md`, `AGENTS.md` |
| **Logging** | `logs/` with timestamped files + latest symlink |
| **Version bumping** | `scripts/bump_version.sh [major|minor|patch]` |

### Standard Script Names

| Script | Purpose | All repos? |
|:-------|:--------|:-----------|
| `dev.sh` | Enter Nix dev environment | Yes |
| `build.sh` | Build the project | C++ repos (Icarus, Daedalus) |
| `test.sh` | Run test suite | Yes |
| `ci.sh` | Full CI pipeline | Yes |
| `coverage.sh` | Generate coverage report | Yes |
| `clean.sh` | Clean build artifacts | Yes |
| `generate_docs.sh` | Generate Doxygen docs | Yes |
| `install-hooks.sh` | Install git hooks + beads merge driver | Yes |
| `bump_version.sh` | Bump semantic version | Yes |

### GitHub Actions Pattern

All workflows share this preamble:

```yaml
steps:
  - uses: actions/checkout@v4
  - uses: cachix/install-nix-action@v20
    with:
      nix_path: nixpkgs=channel:nixos-unstable
  - uses: cachix/cachix-action@v15
    with:
      name: tanged123
      authToken: '${{ secrets.CACHIX_AUTH_TOKEN }}'
```

### What Varies Per Project

| Aspect | Icarus | Hermes | Daedalus |
|:-------|:-------|:-------|:---------|
| Language | C++20 | Python 3.11+ | C++20 |
| Build system | CMake + Ninja | pyproject.toml (hatchling) | CMake + Ninja |
| Formatters | clang-format, nixfmt, cmake-format | ruff, nixfmt | clang-format, nixfmt, cmake-format |
| Lint/typecheck | N/A (compiler) | ruff check, mypy --strict | N/A (compiler) |
| Test framework | GoogleTest | pytest | GoogleTest |
| Version source | CMakeLists.txt | pyproject.toml | CMakeLists.txt |
| Nix stdenv | llvmPackages_latest | default | llvmPackages_latest |
| Nix inputs (deps) | janus, vulcan | icarus | hermes |

---

## 4. Repository Structure

```
daedalus/
├── flake.nix                    # Nix package definition
├── flake.lock                   # Nix dependency lock
├── CMakeLists.txt               # Build configuration
├── Doxyfile                     # Documentation generation
├── .clang-format                # C++ formatting rules
├── README.md                    # Project overview
├── CLAUDE.md                    # Points to .agent/rules/
├── AGENTS.md                    # Beads workflow for agents
│
├── include/daedalus/            # Public headers
│   ├── app.hpp                  # Application lifecycle
│   ├── protocol/                # Hermes protocol client
│   │   ├── client.hpp           # WebSocket client (IXWebSocket)
│   │   ├── schema.hpp           # Schema parsing
│   │   └── telemetry.hpp        # Binary telemetry decoder
│   ├── views/                   # UI views
│   │   ├── plotter.hpp          # Signal plotter (ImPlot)
│   │   ├── topology.hpp         # Topology (node editor)
│   │   ├── console.hpp          # Console/log view
│   │   └── world.hpp            # 3D world view (future)
│   └── data/                    # Data management
│       ├── signal_buffer.hpp    # Ring buffer for per-signal history
│       ├── signal_tree.hpp      # Hierarchical signal browser
│       └── telemetry_queue.hpp  # Lock-free inter-thread queue
│
├── src/                         # Implementation
│   ├── main.cpp                 # Entry point
│   └── daedalus/                # Implementation files
│       ├── app.cpp
│       ├── protocol/
│       ├── views/
│       └── data/
│
├── tests/                       # GoogleTest suite
│   └── test_main.cpp            # Test entry
│
├── docs/                        # Documentation
│   └── daedalus_bootstrap_guide.md  # This file
│
├── references/                  # Git submodules
│   └── hermes/                  # Hermes reference (protocol docs + examples)
│
├── scripts/                     # Development scripts
│   ├── dev.sh
│   ├── build.sh
│   ├── test.sh
│   ├── ci.sh
│   ├── coverage.sh
│   ├── clean.sh
│   ├── generate_docs.sh
│   ├── install-hooks.sh
│   └── bump_version.sh
│
├── .github/
│   ├── hooks/
│   │   └── pre-commit           # treefmt auto-format hook
│   └── workflows/
│       ├── ci.yml               # Build + test
│       ├── format.yml           # Format check
│       ├── docs.yml             # Generate + deploy docs
│       └── coverage.yml         # Coverage report
│
└── .agent/
    └── rules/
        └── daedalus.md          # Master agent ruleset
```

---

## 5. Nix Setup

### Flake Inputs

Daedalus follows the Pantheon dependency chain:

```
daedalus → hermes → icarus → {janus, vulcan, nixpkgs}
```

The `flake.nix` declares:

- `hermes` as a flake input (provides the `hermes` CLI for test data + protocol reference)
- `nixpkgs.follows = "hermes/nixpkgs"` to ensure consistent package versions
- `treefmt-nix` for code formatting
- `flake-utils` for multi-system support

### Dev Shell

The dev shell provides:

- C++ compiler (LLVM/Clang via `llvmPackages_latest`)
- CMake + Ninja build system
- System-level dependencies for ImGui Bundle (OpenGL, GLFW, X11/Wayland, freetype)
- nlohmann_json for JSON parsing
- GoogleTest for testing
- Doxygen + Graphviz for documentation
- treefmt for formatting
- ccache for build acceleration
- **Hermes** (from flake input) for running test simulations

### ImGui Bundle as a Nix Derivation

**Why not just `pkgs.imgui-bundle`?** ImGui Bundle is not in nixpkgs. It consists of 23 tightly-coupled forked Git submodules (Dear ImGui, Hello ImGui, ImPlot, imgui-node-editor, etc.) maintained by the bundle author on custom branches. The individual `pkgs.imgui` in nixpkgs is just the raw core library — no backends, no docking, no ImPlot.

**Strategy**: Build ImGui Bundle as a custom Nix derivation using `fetchgit` with `fetchSubmodules = true`. This fetches all 23 submodules at fixed revisions during the Nix fetch phase (before the sandboxed build begins), keeping the build fully reproducible.

```nix
# Sketch of the custom derivation (in flake.nix)
imgui-bundle = stdenv.mkDerivation {
  pname = "imgui-bundle";
  version = "<pinned-tag>";
  src = fetchgit {
    url = "https://github.com/pthom/imgui_bundle.git";
    rev = "<pinned-commit>";
    hash = "sha256-...";
    fetchSubmodules = true;
  };
  nativeBuildInputs = [ cmake pkg-config ];
  buildInputs = [
    glfw libGL freetype
    xorg.libX11 xorg.libXrandr xorg.libXinerama xorg.libXcursor xorg.libXi
  ];
  cmakeFlags = [
    "-DHELLOIMGUI_USE_GLFW3=ON"
    "-DHELLOIMGUI_HAS_OPENGL3=ON"
  ];
};
```

This gives us a single Nix-cached dependency that provides:

- Dear ImGui (with docking)
- Hello ImGui (windowing, DPI, layout persistence)
- ImPlot (signal plotting)
- imgui-node-editor (topology view)
- All backends wired up and version-compatible

### Entering the Environment

```bash
# Interactive shell
./scripts/dev.sh

# Run a command inside the environment
./scripts/dev.sh cmake --version
```

---

## 6. CMake Configuration

Daedalus uses CMake with the following structure:

```cmake
project(daedalus VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)  # C++20 — see rationale below
```

### Key CMake Options

| Option | Default | Purpose |
|:-------|:--------|:--------|
| `BUILD_TESTING` | `ON` | Build GoogleTest suite |
| `ENABLE_COVERAGE` | `OFF` | Enable `--coverage` flags |

### Why C++20?

The original plan used C++17 for future WASM (Emscripten) compatibility. However:

- WASM is Phase 6 — the final phase. Making every earlier phase pay the cost of a constraint for Phase 6 is premature.
- Emscripten's C++20 support is mature and improving rapidly. By the time we reach Phase 6, coverage will be even better.
- C++20 gives us `std::format`, concepts, ranges, better constexpr, `std::span`, and designated initializers — all of which reduce boilerplate in a data-heavy visualization app.
- If specific C++20 features cause WASM issues in Phase 6, we can address them with targeted `#ifdef` shims rather than constraining the entire project upfront.

### FetchContent Dependencies

Dependencies not available in nixpkgs are pulled via CMake FetchContent:

```cmake
include(FetchContent)

# IXWebSocket — lightweight C++ WebSocket client
FetchContent_Declare(
  ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG        v11.4.6
)
FetchContent_MakeAvailable(ixwebsocket)
```

When building inside the Nix dev shell, system dependencies (OpenGL, GLFW, zlib, OpenSSL) are already available. FetchContent only fetches source-only C++ libraries that aren't in nixpkgs.

---

## 7. Scripts Directory

All scripts auto-detect the Nix environment and re-enter it if needed:

```bash
# This pattern appears at the top of every script:
if [ -z "$IN_NIX_SHELL" ]; then
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    "$SCRIPT_DIR/dev.sh" "$0" "$@"
    exit $?
fi
```

| Script | Description |
|:-------|:------------|
| `dev.sh` | Enter Nix dev shell or run a command within it |
| `build.sh` | CMake configure + Ninja build. Supports `--debug`, `--release`, `--clean`, `-j N` |
| `test.sh` | Build (if needed) + run ctest |
| `ci.sh` | Clean release build + tests (what CI runs) |
| `coverage.sh` | Build with `--coverage`, run tests, generate lcov HTML report |
| `clean.sh` | Remove `build/` directory |
| `generate_docs.sh` | Run Doxygen, output to `build/docs/html` |
| `install-hooks.sh` | Install pre-commit hook + beads merge driver |
| `bump_version.sh` | Bump version in `CMakeLists.txt` + `flake.nix` |

---

## 8. Technology Stack

### Core Libraries

| Layer | Library | Purpose | Acquisition | Phase |
|:------|:--------|:--------|:------------|:------|
| **UI Framework** | ImGui Bundle | Hello ImGui + ImGui + ImPlot + node-editor in one package | Custom Nix derivation | MVP |
| **Networking** | IXWebSocket | WebSocket client (auto-reconnect, TLS, compression) | CMake FetchContent | MVP |
| **JSON** | nlohmann_json | JSON parsing for control channel | nixpkgs | MVP |
| **Testing** | GoogleTest | Unit and integration tests | nixpkgs | MVP |
| **3D World** | TBD (see Phase 5 notes) | Geospatial rendering | TBD | Phase 5 |

### Why ImGui Bundle?

The ImGui ecosystem is not a single library — it is a collection of loosely coupled projects:

- **Dear ImGui** (core): Immediate-mode widget library. No windowing, no rendering backend.
- **Hello ImGui**: Adds windowing (GLFW/SDL), rendering backends (OpenGL/Vulkan/Metal), docking, DPI handling, layout persistence.
- **ImPlot**: Plotting extension for ImGui. Requires ImGui context.
- **imgui-node-editor**: Node graph editor extension. Requires ImGui context.
- **ImGui Bundle**: Combines all of the above into one CMake target with version-compatible forks.

Wiring these together manually (choosing GLFW version, OpenGL backend, ImGui docking branch, matching ImPlot version) is significant integration work that ImGui Bundle eliminates. One dependency, one CMake target, everything works together.

### Why IXWebSocket (not libwebsockets)?

libwebsockets (lws) is a C library with a complex callback-driven event loop model. IXWebSocket provides:

- Simple C++ API: `ws.send()`, `ws.setOnMessageCallback()`
- Built-in auto-reconnect with configurable backoff
- TLS and per-message compression out of the box
- No Boost dependency
- Dramatically less integration code (~10 lines vs. ~200+ for lws)

IXWebSocket is not in nixpkgs, so it is fetched via CMake FetchContent. Its only system dependencies (zlib, OpenSSL) are provided by the Nix dev shell.

---

## 9. Hermes Protocol Reference

This section documents the **actual** Hermes WebSocket protocol as implemented. The authoritative reference is `references/hermes/docs/user_guide/websocket.md`.

### 9.1 Control Channel (JSON over WebSocket Text)

**Schema (Server → Client, sent immediately on connect):**

```json
{
  "type": "schema",
  "modules": {
    "rocket": {
      "signals": [
        {"name": "position.x", "type": "f64", "unit": "m"},
        {"name": "position.y", "type": "f64", "unit": "m"},
        {"name": "velocity.x", "type": "f64", "unit": "m/s"},
        {"name": "velocity.y", "type": "f64", "unit": "m/s"}
      ]
    },
    "inputs": {
      "signals": [
        {"name": "throttle", "type": "f64"}
      ]
    }
  }
}
```

Note: Signals are an **array of objects** (not an object-of-objects). Each signal has `name`, `type`, and optionally `unit`.

**Commands (Client → Server):**

```json
{"action": "subscribe", "params": {"signals": ["*"]}}
{"action": "subscribe", "params": {"signals": ["rocket.position.*"]}}
{"action": "pause"}
{"action": "resume"}
{"action": "reset"}
{"action": "step", "params": {"count": 10}}
{"action": "set", "params": {"signal": "inputs.throttle", "value": 1.0}}
```

Note: Commands use `{"action": "..."}` at the top level — there is no `"type": "cmd"` wrapper.

**Acknowledgments (Server → Client):**

```json
{"type": "ack", "action": "subscribe", "count": 4, "signals": ["rocket.position.x", "rocket.position.y", "rocket.velocity.x", "rocket.velocity.y"]}
{"type": "ack", "action": "pause"}
{"type": "ack", "action": "resume"}
{"type": "ack", "action": "set", "signal": "inputs.throttle", "value": 1.0}
{"type": "ack", "action": "step", "count": 10, "frame": 110}
```

The subscribe ack is especially important: it returns the **resolved signal list in order**. This order determines the binary telemetry payload layout.

**Events (Server → Client):**

```json
{"type": "event", "event": "paused"}
{"type": "event", "event": "running"}
{"type": "event", "event": "reset"}
```

**Errors (Server → Client):**

```json
{"type": "error", "message": "Unknown signal: foo.bar"}
{"type": "error", "message": "Invalid JSON"}
```

### 9.2 Telemetry Channel (Binary over WebSocket Binary)

All multi-byte fields are **little-endian**.

```
┌──────────────────────────────────────────────────────┐
│ Header (24 bytes)                                     │
├────────────┬────────────┬────────────┬───────────────┤
│ Magic (4B) │ Frame (8B) │ Time (8B)  │ Count (4B)    │
│ 0x48455254 │ uint64_le  │ float64_le │ uint32_le     │
│ "HERT"     │ frame num  │ sim time   │ # of values   │
├────────────┴────────────┴────────────┴───────────────┤
│ Payload (count × 8 bytes)                             │
│ [value0: f64_le, value1: f64_le, ...]                 │
└──────────────────────────────────────────────────────┘
```

- **Magic**: `0x48455254` ("HERT" in little-endian) — validate on every frame to detect corruption
- **Frame**: Monotonic simulation frame counter (uint64)
- **Time**: Simulation time in seconds (float64)
- **Count**: Number of signal values in payload (uint32)
- **Payload**: Signal values in **subscription order** (as returned by the subscribe ack)

### 9.3 Decoding Example (C++)

```cpp
struct TelemetryHeader {
    uint32_t magic;
    uint64_t frame;
    double   time;
    uint32_t count;
};
static_assert(sizeof(TelemetryHeader) == 24);

bool decode_frame(const uint8_t* data, size_t len,
                  TelemetryHeader& hdr, std::span<const double>& values) {
    if (len < sizeof(TelemetryHeader)) return false;
    std::memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != 0x48455254) return false;
    if (len < sizeof(hdr) + hdr.count * sizeof(double)) return false;

    values = std::span<const double>(
        reinterpret_cast<const double*>(data + sizeof(hdr)), hdr.count);
    return true;
}
```

### 9.4 Bandwidth Estimation

| Signals | Rate | Frame Size | Bandwidth |
|:--------|:-----|:-----------|:----------|
| 10 | 60 Hz | 24 + 80 = 104 B | 6 KB/s |
| 100 | 60 Hz | 24 + 800 = 824 B | 48 KB/s |
| 500 | 60 Hz | 24 + 4000 = 4024 B | 236 KB/s |

At 500 signals, a 5-minute history at full rate = ~70 MB. Per-signal ring buffers should be sized accordingly (see Section 10).

---

## 10. Data Pipeline & Threading Model

ImGui is single-threaded — all ImGui calls must happen on the main/render thread. Network I/O must never block rendering. This requires a clear separation between the network thread and the render thread.

### Architecture

```
Network Thread (IXWebSocket)              Main/Render Thread (ImGui)
────────────────────────────              ─────────────────────────
WebSocket callbacks fire                  Each frame (~16ms at 60fps):
  ├─ Text frame (JSON):                    ├─ Poll TelemetryQueue
  │   Parse JSON                           │   └─ For each dequeued frame:
  │   Post event to EventQueue             │       Update SignalBuffers
  │                                        │
  ├─ Binary frame (telemetry):             ├─ Poll EventQueue
  │   Validate magic + length              │   └─ Handle events (schema,
  │   Copy raw bytes into                  │       acks, state changes)
  │   TelemetryQueue (SPSC)                │
  │                                        ├─ ImGui::NewFrame()
  └─ Connection state changes:             ├─ Render all views
      Post to EventQueue                   │   (plotter, console, topology)
                                           └─ ImGui::Render()
```

### Key Data Structures

| Structure | Thread Safety | Purpose |
|:----------|:-------------|:--------|
| `TelemetryQueue` | SPSC lock-free ring | Raw binary frames from network → render thread |
| `EventQueue` | SPSC lock-free ring | JSON events/acks from network → render thread |
| `SignalBuffer` | Render thread only | Per-signal rolling history for plotting |
| `SignalTree` | Render thread only | Hierarchical signal namespace built from schema |
| `SignalRegistry` | Render thread only | Maps signal index (from subscribe ack) → SignalBuffer |

### Memory Budget

For per-signal history (SignalBuffer), target a **fixed memory budget** rather than a fixed time window:

| Parameter | Default | Notes |
|:----------|:--------|:------|
| History per signal | 18,000 samples | 5 minutes at 60 Hz |
| Bytes per signal buffer | ~144 KB | 18,000 × 8 bytes (f64) |
| Max subscribed signals | 500 | Soft limit, warn in UI if exceeded |
| Total history memory | ~70 MB | 500 × 144 KB |
| TelemetryQueue depth | 256 frames | ~1-4 seconds of buffering |

Older data can be decimated (e.g., keep every Nth sample beyond 1 minute) to extend history with constant memory.

---

## 11. Development Workflow

### Using Hermes as a Live Data Source

Since Hermes is a Nix flake input, it is available in the dev shell. You can run real simulations that stream telemetry to Daedalus over WebSocket.

**Terminal 1 — Start a Hermes simulation:**

```bash
# Simple mock vehicle (4 signals, sinusoidal data)
python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml

# Full Icarus rocket ascent (~90 signals, real 6DOF physics)
python -m hermes.cli.main run references/hermes/examples/icarus_rocket.yaml
```

**Terminal 2 — Run Daedalus:**

```bash
./scripts/build.sh && ./build/daedalus
```

The `websocket_telemetry.yaml` example is ideal for initial development — it provides 4 mock signals (position.x/y, velocity.x/y) with predictable sinusoidal behavior. The `icarus_rocket.yaml` example provides the full Icarus 6DOF simulation with ~90 real physics signals, but requires Icarus to be built (it's pulled in transitively through the Nix dependency chain).

### Quick Validation with the Python Client

Hermes ships a reference WebSocket client that's useful for verifying the server is working:

```bash
python references/hermes/examples/websocket_client.py
```

This prints the schema, subscribes to signals, and dumps telemetry frames to the console. Use it to sanity-check the protocol before debugging Daedalus.

### Development Cycle

```bash
# 1. Start Hermes in one terminal
python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml

# 2. Build and run Daedalus
./scripts/build.sh && ./build/daedalus

# 3. Run tests
./scripts/test.sh

# 4. Full CI check before committing
./scripts/ci.sh
```

---

## 12. Testing Framework

Daedalus uses **GoogleTest** (matching Icarus convention for C++ repos).

### Test Organization

```
tests/
├── test_main.cpp          # GTest main + placeholder
├── protocol/              # Protocol parsing tests
│   ├── test_schema.cpp    # Schema JSON parsing
│   └── test_telemetry.cpp # Binary telemetry decoding
├── data/                  # Data structure tests
│   ├── test_signal_buffer.cpp  # Ring buffer tests
│   ├── test_signal_tree.cpp    # Signal hierarchy tests
│   └── test_telemetry_queue.cpp # Lock-free queue tests
└── views/                 # View logic tests (non-rendering)
```

### Running Tests

```bash
./scripts/test.sh                    # Run all tests
./scripts/ci.sh                      # Clean release build + tests
./scripts/coverage.sh                # Tests with coverage report
```

### What to Test

- **Telemetry decoding**: Magic validation, header parsing, payload extraction, malformed frames
- **Schema parsing**: JSON schema → SignalTree, SignalRegistry construction
- **Signal buffers**: Ring buffer push/capacity/overflow, data integrity at boundaries
- **Signal tree**: Hierarchical namespace building from flat `module.signal.name` strings
- **Subscribe ack parsing**: Signal order resolution, index mapping

### What NOT to Test (in unit tests)

- OpenGL rendering output
- ImGui widget appearance
- Actual WebSocket connectivity (test protocol parsing with raw byte arrays)

---

## 13. CI/CD Workflows

Four GitHub Actions workflows, matching the Pantheon pattern:

| Workflow | Trigger | Job |
|:---------|:--------|:----|
| `ci.yml` | Every push | Clean release build + tests |
| `format.yml` | Every push | `nix fmt -- --fail-on-change` |
| `docs.yml` | Push to `main` | Generate Doxygen docs, deploy to GitHub Pages |
| `coverage.yml` | Every push | Build with coverage, upload to Codecov |

All workflows use the shared Nix + Cachix preamble.

---

## 14. Agent Rules

### File Layout

```
CLAUDE.md                    # Entry point → points to .agent/rules/daedalus.md
AGENTS.md                    # Beads (bd) workflow for agents
.agent/rules/daedalus.md     # Master ruleset (always_on trigger)
```

### Key Rules for Daedalus

1. **Rendering Safety**: Never block render thread. All network I/O on background thread. Use lock-free queues between threads.
2. **Protocol Compliance**: All data comes from the Hermes protocol (Section 9). No physics knowledge. Validate against `references/hermes/docs/user_guide/websocket.md`.
3. **ImGui Patterns**: Use ID stack (`PushID`/`PopID`), docked windows via Hello ImGui, layout persistence.
4. **C++20**: Use modern C++ features. WASM compatibility deferred to Phase 6.
5. **No raw new/delete**: RAII, smart pointers, `std::span`, value types.

---

## 15. Git Configuration

### .gitignore

Covers C++ build artifacts, IDE files, logs, Nix results, and build directories. Explicitly preserves `.beads/` for issue tracking.

### .gitattributes

Configures the beads merge driver:

```
.beads/issues.jsonl merge=beads
```

### Pre-commit Hook

Located at `.github/hooks/pre-commit`, installed via `./scripts/install-hooks.sh`:

```bash
nix fmt
git add -u
```

### References Submodule

Hermes is included as a reference submodule for protocol documentation and test examples:

```bash
git submodule add https://github.com/tanged123/hermes.git references/hermes
```

---

## 16. Implementation Roadmap

### Phase 1: Skeleton + Protocol Client (MVP)

- [x] Repository bootstrap (this guide)
- [x] Nix flake + dev shell
- [x] CMake build system
- [x] Scripts directory (full set)
- [x] CI/CD workflows
- [x] Agent rules
- [x] ImGui Bundle custom Nix derivation + CMake integration
- [x] IXWebSocket FetchContent integration
- [x] Connect to Hermes, receive schema, parse into SignalTree
- [x] Binary telemetry decoder (validate against `websocket_telemetry.yaml`)
- [x] TelemetryQueue (SPSC lock-free ring buffer)
- [x] Basic ImGui window with connection status + signal tree browser
- [x] Verify end-to-end: Hermes → WebSocket → Daedalus → signals visible in tree

### Phase 2: Signal Plotter

- [x] ImPlot integration (provided by ImGui Bundle)
- [x] SignalBuffer (per-signal rolling history ring buffer)
- [x] Drag-and-drop signals from tree onto plot areas
- [x] Scrolling time-series plots with auto-scaling
- [x] Multiple Y-axes with independent scaling
- [x] Cursors, markers, statistics overlay (min/max/mean)

### Phase 3: Playback Controls + Console

- [ ] Pause / resume / reset / single-step buttons (send commands to Hermes)
- [ ] Event stream display (acks, state changes, errors)
- [ ] Command history with replay
- [ ] Signal value inspector (current values table, searchable)

### Phase 4: Topology + Inspection

- [ ] imgui-node-editor integration (provided by ImGui Bundle)
- [ ] Auto-layout from schema wiring information
- [ ] Signal values displayed on hover over connections
- [ ] Inspect command → shadow execution pipeline
- [ ] Subgraph rendering with intermediate values

### Phase 5: 3D World View

> **Note**: This is the most complex single feature. The technology choice is deliberately deferred.

- [ ] Evaluate options: raw OpenGL globe, lightweight rendering library, or osgEarth
- [ ] Vehicle position rendering (lat/lon/alt from telemetry)
- [ ] Attitude visualization (body axes, velocity vector)
- [ ] Trajectory trail with time-based coloring
- [ ] Camera modes: Earth-fixed, vehicle-fixed, free-fly

**Why defer the 3D tech choice?** osgEarth is extremely heavy (OpenSceneGraph + GDAL + PROJ + terrain tiles). A simpler approach (colored dot on a Mercator projection rendered in ImGui, or a basic OpenGL sphere) may provide 80% of the value at 5% of the complexity. By Phase 5, we'll have enough experience with the codebase to make a better decision. The WASM story for osgEarth is also unclear — maintaining two separate 3D stacks (osgEarth for desktop, CesiumJS for web) would be a maintenance burden.

### Phase 6: Polish + WASM

- [ ] Layout persistence (save/restore window arrangement via Hello ImGui)
- [ ] Theming (dark/light, futuristic mission control palette)
- [ ] Evaluate C++20 → Emscripten WASM compatibility; apply targeted shims if needed
- [ ] VS Code webview panel integration
- [ ] Multi-Hermes connection support

---

## 17. Verification Checklist

After bootstrapping, verify the following:

### Structure

- [ ] `flake.nix` exists and `nix flake check` passes
- [ ] `CMakeLists.txt` exists with `project(daedalus VERSION 0.1.0)`
- [ ] `include/daedalus/app.hpp` exists
- [ ] `src/main.cpp` exists and compiles
- [ ] `tests/test_main.cpp` exists with placeholder test

### Scripts

- [ ] All scripts in `scripts/` are executable (`chmod +x`)
- [ ] `./scripts/dev.sh` enters Nix dev shell
- [ ] `./scripts/build.sh` builds successfully
- [ ] `./scripts/test.sh` runs tests (placeholder passes)
- [ ] `./scripts/ci.sh` runs full CI pipeline
- [ ] `./scripts/clean.sh` removes build directory
- [ ] `./scripts/install-hooks.sh` installs pre-commit hook

### CI/CD

- [ ] `.github/workflows/ci.yml` exists
- [ ] `.github/workflows/format.yml` exists
- [ ] `.github/workflows/docs.yml` exists
- [ ] `.github/workflows/coverage.yml` exists
- [ ] `.github/hooks/pre-commit` exists and is executable

### Agent Infrastructure

- [ ] `CLAUDE.md` points to `.agent/rules/daedalus.md`
- [ ] `AGENTS.md` documents beads workflow
- [ ] `.agent/rules/daedalus.md` has `trigger: always_on`

### Git

- [ ] `.gitignore` covers C++ artifacts, logs, build, Nix results
- [ ] `.gitattributes` configures beads merge driver
- [ ] `Doxyfile` configured for C++ (`.hpp`, `.cpp`, `.md`)
- [ ] `references/hermes` submodule present and initialized

### Documentation

- [ ] `README.md` with project overview and quick start
- [ ] `docs/daedalus_bootstrap_guide.md` (this file)

### Development Data Source

- [ ] `python -m hermes.cli.main run references/hermes/examples/websocket_telemetry.yaml` starts successfully
- [ ] `python references/hermes/examples/websocket_client.py` connects and receives telemetry
