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
10. [Testing Framework](#10-testing-framework)
11. [CI/CD Workflows](#11-cicd-workflows)
12. [Agent Rules](#12-agent-rules)
13. [Git Configuration](#13-git-configuration)
14. [Implementation Roadmap](#14-implementation-roadmap)
15. [Verification Checklist](#15-verification-checklist)

---

## 1. Project Overview

### What is Daedalus?

**Daedalus** is a high-density Mission Control visualization suite for aerospace simulation. Named after the mythological craftsman who built the labyrinth and fashioned wings for his son Icarus, Daedalus provides the tools to observe and understand simulation behavior.

Daedalus provides:

- **Signal Plotter**: Drag-and-drop signal visualization using ImPlot
- **3D World View**: Vehicle position/attitude rendered on Earth via osgEarth
- **Topology View**: Module wiring diagrams via imgui-node-editor
- **Console/Log View**: Event stream, phase transitions, command history
- **Inspect Mode**: Shadow execution subgraph rendering for debugging

### Design Principles

> **"Daedalus knows nothing about Icarus."**

Daedalus is a generic visualization client for the Hermes protocol. It has no knowledge of physics, simulation internals, or specific module implementations. All data arrives through the Hermes WebSocket protocol as generic signals.

- **Protocol-Driven**: Everything comes from Hermes via WebSocket
- **Density-First**: Maximize information per pixel, mission control aesthetic
- **Portable**: Desktop (native C++) and browser (WASM via Emscripten) targets

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

Daedalus communicates with Hermes over a two-channel WebSocket protocol:

| Channel | Transport | Content |
|:--------|:----------|:--------|
| **Control** | WebSocket Text (JSON) | Schema, commands, events |
| **Telemetry** | WebSocket Binary | Signal values at configured rate |

### Connection Lifecycle

1. Client connects to `ws://host:port`
2. Server sends `schema` message (all modules, signals, topology)
3. Client sends `subscribe` commands for signals of interest
4. Server streams binary telemetry frames at configured rate (default 60 Hz)
5. Client sends control commands (pause, resume, reset, set, inspect)
6. Server sends events (state changes, phase changes, errors)

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
| Language | C++20 | Python 3.11+ | C++17 |
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
│   │   ├── client.hpp           # WebSocket client
│   │   ├── schema.hpp           # Schema parsing
│   │   └── telemetry.hpp        # Binary telemetry decoder
│   ├── views/                   # UI views
│   │   ├── plotter.hpp          # Signal plotter (ImPlot)
│   │   ├── world.hpp            # 3D world view (osgEarth)
│   │   ├── topology.hpp         # Topology (node editor)
│   │   └── console.hpp          # Console/log view
│   └── data/                    # Data management
│       ├── signal_buffer.hpp    # Ring buffer for history
│       └── signal_tree.hpp      # Hierarchical signal browser
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
│   └── hermes/                  # Hermes reference (for protocol docs)
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
- `hermes` as a flake input (for protocol reference and potential future bindings)
- `nixpkgs.follows = "hermes/nixpkgs"` to ensure consistent package versions
- `treefmt-nix` for code formatting
- `flake-utils` for multi-system support

### Dev Shell

The dev shell provides:
- C++ compiler (LLVM/Clang via `llvmPackages_latest`)
- CMake + Ninja build system
- ImGui ecosystem libraries (glfw, OpenGL)
- libwebsockets for Hermes protocol
- nlohmann_json for JSON parsing
- GoogleTest for testing
- Doxygen + Graphviz for documentation
- treefmt for formatting
- ccache for build acceleration

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
set(CMAKE_CXX_STANDARD 17)  # C++17 for WASM compatibility
```

### Key CMake Options

| Option | Default | Purpose |
|:-------|:--------|:--------|
| `BUILD_TESTING` | `ON` | Build GoogleTest suite |
| `ENABLE_COVERAGE` | `OFF` | Enable `--coverage` flags |

### Why C++17 (not C++20)?

Daedalus targets both native desktop and WASM (via Emscripten). C++17 has broader Emscripten support. Icarus uses C++20 because it never targets WASM.

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

| Layer | Library | Purpose | Phase |
|:------|:--------|:--------|:------|
| **Windowing** | Hello ImGui | DPI handling, docking, layout persistence | MVP |
| **Widgets** | Dear ImGui | Immediate-mode UI framework | MVP |
| **Plotting** | ImPlot | High-frequency signal visualization | MVP |
| **Topology** | imgui-node-editor | Block diagram / wiring visualization | Phase 5 |
| **3D World** | osgEarth (desktop) / CesiumJS (web) | Geospatial rendering | Phase 4 |
| **Networking** | libwebsockets | WebSocket client for Hermes protocol | MVP |
| **JSON** | nlohmann_json | JSON parsing for control channel | MVP |
| **Testing** | GoogleTest | Unit and integration tests | MVP |

### Dependency Acquisition Strategy

For the MVP, dependencies that are available in nixpkgs are used directly. Libraries that need to be built from source (Hello ImGui, ImPlot, imgui-node-editor) should be added as:

1. **CMake FetchContent** (preferred for header-only or small libs)
2. **Nix flake inputs** (for larger deps that benefit from caching)
3. **Git submodules in `references/`** (for reference/documentation only)

### ImGui Ecosystem Notes

The ImGui ecosystem is not a single library — it is a collection of loosely coupled projects:

- **Dear ImGui** (core): Immediate-mode widget library. No windowing, no rendering backend.
- **Hello ImGui**: Adds windowing (GLFW/SDL), rendering backends (OpenGL/Vulkan/Metal), docking, DPI.
- **ImGui Bundle**: Combines Hello ImGui + ImPlot + imgui-node-editor + more. One CMake target.
- **ImPlot**: Plotting extension for ImGui. Requires ImGui context.
- **imgui-node-editor**: Node graph editor extension. Requires ImGui context.

**Recommendation**: Use **ImGui Bundle** as a single dependency that provides the full stack. It handles CMake integration, backend selection, and version compatibility.

---

## 9. Hermes Protocol Reference

This section summarizes the Hermes protocol that Daedalus must implement as a client.

### 9.1 Control Channel (JSON over WebSocket Text)

**Schema (Server → Client on connect):**
```json
{
  "type": "schema",
  "version": "0.2",
  "modules": {
    "icarus": {
      "signals": {
        "Vehicle.position.x": {"type": "f64", "unit": "m"},
        "Vehicle.position.y": {"type": "f64", "unit": "m"},
        "Vehicle.attitude":   {"type": "quat", "unit": ""}
      },
      "commands": ["pause", "reset", "set_signal", "inspect"]
    }
  },
  "wiring": [
    {"src": "icarus.Vehicle.attitude", "dst": "gnc.nav.attitude_in"}
  ],
  "topology": {
    "nodes": [...],
    "edges": [...]
  }
}
```

**Commands (Client → Server):**
```json
{"type": "cmd", "action": "pause"}
{"type": "cmd", "action": "resume"}
{"type": "cmd", "action": "reset"}
{"type": "cmd", "action": "step", "count": 1}
{"type": "cmd", "action": "set", "signal": "icarus.Vehicle.position.z", "value": 1000.0}
{"type": "cmd", "action": "subscribe", "signals": ["icarus.Vehicle.position.*"]}
{"type": "cmd", "action": "unsubscribe", "signals": ["icarus.Vehicle.velocity.*"]}
{"type": "cmd", "action": "inspect", "module": "icarus", "frame": 12345}
```

**Events (Server → Client):**
```json
{"type": "event", "name": "state_changed", "state": "paused"}
{"type": "event", "name": "phase_changed", "phase": "COAST", "time": 125.3}
{"type": "event", "name": "error", "module": "gnc", "message": "Watchdog timeout"}
```

### 9.2 Telemetry Channel (Binary over WebSocket Binary)

```
┌────────────────────────────────────────────────────────────┐
│ Header (16 bytes)                                          │
├────────────┬────────────┬────────────┬────────────────────┤
│ frame (u32)│ time (f64) │ count (u16)│ reserved (2 bytes) │
├────────────┴────────────┴────────────┴────────────────────┤
│ Payload (count × 8 bytes)                                  │
├───────────────────────────────────────────────────────────┤
│ signal[0] (f64) │ signal[1] (f64) │ ... │ signal[N] (f64) │
└───────────────────────────────────────────────────────────┘
```

- **Frame**: Monotonic frame counter (u32)
- **Time**: Mission Elapsed Time in seconds (f64)
- **Count**: Number of signals in payload (u16)
- **Payload**: Signal values in subscription order

Signal order is determined by the `subscribe` command sequence and the schema.

---

## 10. Testing Framework

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
│   └── test_signal_tree.cpp    # Signal hierarchy tests
└── views/                 # View logic tests (non-rendering)
```

### Running Tests

```bash
./scripts/test.sh                    # Run all tests
./scripts/ci.sh                      # Clean release build + tests
./scripts/coverage.sh                # Tests with coverage report
```

### What to Test

- **Protocol parsing**: JSON schema → internal data structures
- **Binary decoding**: Raw bytes → typed signal values
- **Signal buffers**: Ring buffer capacity, overflow, data integrity
- **Signal tree**: Hierarchical namespace building from flat signal list
- **Wiring config**: Topology graph construction from schema

### What NOT to Test (in unit tests)

- OpenGL rendering output
- ImGui widget appearance
- Network connectivity (use mock WebSocket for integration tests)

---

## 11. CI/CD Workflows

Four GitHub Actions workflows, matching the Pantheon pattern:

| Workflow | Trigger | Job |
|:---------|:--------|:----|
| `ci.yml` | Every push | Clean release build + tests |
| `format.yml` | Every push | `nix fmt -- --fail-on-change` |
| `docs.yml` | Push to `main` | Generate Doxygen docs, deploy to GitHub Pages |
| `coverage.yml` | Every push | Build with coverage, upload to Codecov |

All workflows use the shared Nix + Cachix preamble.

---

## 12. Agent Rules

### File Layout

```
CLAUDE.md                    # Entry point → points to .agent/rules/daedalus.md
AGENTS.md                    # Beads (bd) workflow for agents
.agent/rules/daedalus.md     # Master ruleset (always_on trigger)
```

### Key Rules for Daedalus

1. **Rendering Safety**: Never block render thread. Double-buffer telemetry data.
2. **Protocol Compliance**: All data comes from Hermes protocol. No physics knowledge.
3. **ImGui Patterns**: Use ID stack, docked windows, layout persistence.
4. **C++17**: For WASM compatibility (Emscripten).
5. **No raw new/delete**: RAII, smart pointers only.

---

## 13. Git Configuration

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

To add Hermes as a reference submodule (for protocol documentation):
```bash
git submodule add https://github.com/tanged123/hermes.git references/hermes
```

---

## 14. Implementation Roadmap

### Phase 1: Skeleton + Protocol Client (MVP)

- [x] Repository bootstrap (this guide)
- [x] Nix flake + dev shell
- [x] CMake build system
- [x] Scripts directory (full set)
- [x] CI/CD workflows
- [x] Agent rules
- [ ] Hello ImGui / ImGui Bundle integration (windowing + rendering backend)
- [ ] libwebsockets client connecting to Hermes
- [ ] JSON schema parsing → internal signal registry
- [ ] Binary telemetry decoder
- [ ] Basic ImGui window with connection status

### Phase 2: Signal Plotter

- [ ] Signal tree UI (hierarchical browser from schema)
- [ ] ImPlot integration
- [ ] Drag-and-drop signals onto plot areas
- [ ] Scrolling history buffer (ring buffer)
- [ ] Multiple Y-axes with independent scaling
- [ ] Cursors, markers, statistics overlay

### Phase 3: Playback Controls + Console

- [ ] Pause / resume / reset / single-step buttons
- [ ] Event stream display (state changes, phase changes, errors)
- [ ] Command history with replay
- [ ] Signal value inspector (current values table)

### Phase 4: 3D World View

- [ ] osgEarth integration (render-to-texture into ImGui)
- [ ] Vehicle position rendering on WGS84 Earth
- [ ] Attitude visualization (body axes, velocity vector)
- [ ] Trajectory trail with time-based coloring
- [ ] Camera modes: Earth-fixed, vehicle-fixed, free-fly

### Phase 5: Topology + Inspection

- [ ] imgui-node-editor integration
- [ ] Auto-layout from schema topology field
- [ ] Signal values displayed on hover
- [ ] Inspect command → shadow execution pipeline
- [ ] Subgraph rendering with intermediate values

### Phase 6: Polish + WASM

- [ ] Layout persistence (save/restore window arrangement)
- [ ] Theming (dark/light, futuristic mission control palette)
- [ ] Emscripten WASM build
- [ ] VS Code webview panel integration
- [ ] Multi-Hermes connection support

---

## 15. Verification Checklist

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

### Documentation
- [ ] `README.md` with project overview and quick start
- [ ] `docs/daedalus_bootstrap_guide.md` (this file)
