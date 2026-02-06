---
trigger: always_on
---

# Agent Ruleset: Daedalus Project

You are an advanced AI coding assistant working on **Daedalus**, a Mission Control Visualization Suite for aerospace simulation. Your primary directive is to be **meticulous, detail-oriented, and extremely careful**.

## Project Overview

Daedalus is a **high-density visualization client** that connects to Hermes (the simulation orchestration platform) via WebSocket and renders real-time telemetry data. It knows nothing about physics — it speaks the Hermes protocol and visualizes generic signals.

**Core Views:**
- **Signal Plotter**: Drag-and-drop signal visualization with ImPlot
- **3D World View**: Vehicle position/attitude on Earth (osgEarth)
- **Topology View**: Module wiring diagram (imgui-node-editor)
- **Console/Log**: Event stream, phase transitions, command history
- **Inspect Mode**: Shadow execution subgraph rendering

**Key Principles:**
- **Daedalus knows nothing about Icarus.** It speaks the Hermes protocol and visualizes generic signals.
- **Protocol-Driven**: All data comes through the Hermes WebSocket protocol (JSON control + binary telemetry).
- **Density-First**: Maximize information density in the UI. Futuristic mission control aesthetic.
- **Portable**: Desktop (native) and browser (WASM) targets.

## On Start - Required Reading

**Before writing any code, you MUST read these documents:**

1. **Bootstrap Guide**: `docs/daedalus_bootstrap_guide.md`
   - Repository structure, Nix/CMake setup, script usage
   - Technology stack and dependency rationale
   - Hermes protocol reference

2. **Architecture Document**: Read the Daedalus section (Section 5) of the system architecture:
   `docs/architecture/hermes_daedalus_architecture.md` (if present, or in the hermes repo)

## Workflow: Beads (bd) & Global Sync

This project uses **bd** (beads) for issue tracking. Run `bd onboard` to get started.

### Quick Reference
```bash
bd ready              # Find available work
bd show <id>          # View issue details
bd update <id> --status in_progress  # Claim work
bd close <id>         # Complete work
bd sync               # Sync with git
```

### Landing the Plane (Session Completion)
**When ending a work session**, you MUST complete ALL steps below. Work is NOT complete until `git push` succeeds.

**MANDATORY WORKFLOW:**
1. **File issues for remaining work** - Create issues for anything that needs follow-up
2. **Run quality gates** (if code changed) - `./scripts/ci.sh` (build + tests)
3. **Update issue status** - Close finished work, update in-progress items
4. **PUSH TO REMOTE** - This is MANDATORY:
   ```bash
   git pull --rebase
   bd sync
   git push
   git status  # MUST show "up to date with origin"
   ```
5. **Clean up** - Clear stashes, prune remote branches
6. **Verify** - All changes committed AND pushed
7. **Hand off** - Provide context for next session

**CRITICAL RULES:**
- Work is NOT complete until `git push` succeeds.
- NEVER stop before pushing - that leaves work stranded locally.
- NEVER say "ready to push when you are" - YOU must push.
- If push fails, resolve and retry until it succeeds.

## Global Behavioral Rules

1. **Safety First**: You must NEVER "nuke" a repository. Do not delete large portions of code or directories without explicit, confirmed instructions.

2. **Git Inviolability**:
   - **NEVER** run git commands that modify history (reset, rebase, push --force).
   - **NEVER** commit or push changes automatically unless explicitly asked.
   - **ALWAYS** leave git state management to the user.
   - **Respect .gitignore**: Do not add files that should be ignored.

3. **Meticulousness**:
   - Read all provided context before generating code.
   - Double-check types, APIs, and rendering calls.
   - When refactoring, ensure no functionality is lost.
   - Prefer clarity and correctness over brevity.

4. **No Hallucinations**: Do not invent APIs. Search the codebase first.

5. **Context Preservation**:
   - **Documentation First**: Create and update documentation in `docs/`.
   - **Artifacts**: Use `docs/` for planning and architecture.
   - **Handover**: Write down your plan and progress so the next agent can resume.

## Daedalus-Specific Rules (CRITICAL)

### 1. Rendering Safety (The "Red Line")

**These rules are INVIOLABLE. Breaking them causes rendering glitches or crashes.**

- **Never block the render thread**: WebSocket I/O and data parsing must happen on background threads.
- **Double-buffer telemetry data**: Producer (network) and consumer (render) must not share raw pointers.
- **Frame-rate independence**: All animations and scrolling must use delta-time, not frame count.

### 2. Hermes Protocol Compliance (MANDATORY)

Daedalus is a client of the Hermes protocol. All communication follows:

- **Control Channel** (JSON over WebSocket Text): Commands, events, schema
- **Telemetry Channel** (Binary over WebSocket Binary): Signal values

```
Binary Telemetry Packet:
┌────────────┬────────────┬────────────┬────────────────────┐
│ frame (u32)│ time (f64) │ count (u16)│ reserved (2 bytes) │
├────────────┴────────────┴────────────┴────────────────────┤
│ signal[0] (f64) │ signal[1] (f64) │ ... │ signal[N] (f64) │
└───────────────────────────────────────────────────────────┘
```

### 3. ImGui Patterns

- **ID Stack**: Always push unique IDs for repeated widgets. Use `PushID(i)` / `PopID()`.
- **Window flags**: Prefer docked windows via Hello ImGui's docking API.
- **ImPlot**: Use `ImPlot::SetNextAxesToFit()` for auto-scaling on first data.
- **Layout persistence**: Use Hello ImGui's built-in layout save/restore.

### 4. Coding Style & Standards

- **Language Standard**: C++17 (for broad compatibility including WASM).
- **Formatting**: Adhere to `treefmt` (clang-format) rules.
- **Testing**: Write GoogleTest cases for protocol parsing, data structures, signal buffers.
- **No raw `new`/`delete`**: Use `std::unique_ptr`, `std::shared_ptr`, RAII.

## Project Structure

```
include/daedalus/
├── app.hpp            # Application lifecycle
├── protocol/          # Hermes protocol client
│   ├── client.hpp     # WebSocket client
│   ├── schema.hpp     # Schema parsing
│   └── telemetry.hpp  # Binary telemetry decoder
├── views/             # UI views
│   ├── plotter.hpp    # Signal plotter (ImPlot)
│   ├── world.hpp      # 3D world view (osgEarth)
│   ├── topology.hpp   # Module topology (node editor)
│   └── console.hpp    # Log/event console
└── data/              # Data management
    ├── signal_buffer.hpp  # Ring buffer for signal history
    └── signal_tree.hpp    # Hierarchical signal browser

src/daedalus/
├── app.cpp
├── protocol/
├── views/
└── data/

tests/                 # Test suite
docs/                  # Documentation
```

## Workflow Commands

All scripts auto-enter Nix if needed:

```bash
./scripts/dev.sh          # Enter Nix development environment
./scripts/build.sh        # Build the project
./scripts/test.sh         # Run all tests
./scripts/ci.sh           # Full CI (clean release build + tests)
./scripts/coverage.sh     # Generate coverage report
./scripts/clean.sh        # Clean build artifacts
./scripts/generate_docs.sh # Generate Doxygen docs
./scripts/install-hooks.sh # Install pre-commit hooks
```

**Inside nix develop:**
```bash
cmake -B build -G Ninja && ninja -C build   # Build
ctest --test-dir build --output-on-failure   # Test
```

## Key Dependencies

- **Hello ImGui / ImGui Bundle**: Windowing, docking, DPI
- **ImPlot**: High-frequency signal plotting
- **imgui-node-editor**: Topology/wiring visualization
- **osgEarth**: Geospatial 3D rendering (Phase 4)
- **libwebsockets**: WebSocket client for Hermes protocol
- **nlohmann_json**: JSON parsing for control channel

## Quick Reference

| Need | Use |
|:-----|:----|
| Parse JSON message | `nlohmann::json::parse(text)` |
| Decode binary telemetry | `std::memcpy` from WebSocket binary frame |
| Signal ring buffer | `SignalBuffer::push(value)` / `SignalBuffer::data()` |
| ImPlot line | `ImPlot::PlotLine(label, xs, ys, count)` |
| WebSocket connect | `lws_client_connect_via_info(...)` |
| Unique widget ID | `ImGui::PushID(i)` / `ImGui::PopID()` |
