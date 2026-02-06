# Daedalus

[![Daedalus CI](https://github.com/tanged123/daedalus/actions/workflows/ci.yml/badge.svg)](https://github.com/tanged123/daedalus/actions/workflows/ci.yml)
[![Format Check](https://github.com/tanged123/daedalus/actions/workflows/format.yml/badge.svg)](https://github.com/tanged123/daedalus/actions/workflows/format.yml)
[![codecov](https://codecov.io/github/tanged123/daedalus/graph/badge.svg)](https://codecov.io/github/tanged123/daedalus)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://tanged123.github.io/daedalus/)

**Mission Control Visualization Suite for Project Icarus**

Daedalus is a high-density mission control interface for real-time monitoring and analysis of simulations orchestrated by [Hermes](https://github.com/tanged123/hermes). It speaks the Hermes protocol and visualizes generic signals — it knows nothing about physics or simulation internals.

## Architecture

Daedalus is the visualization layer in the Icarus ecosystem:

```
┌─────────────────────────────────────────────────────────────┐
│                    DAEDALUS (Visualizer)                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐    │
│  │ 3D World │  │ Plotting │  │ Topology │  │ Console  │    │
│  │(osgEarth)│  │ (ImPlot) │  │ (NodeEd) │  │  (Log)   │    │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘    │
└────────────────────────┬────────────────────────────────────┘
                         │ WebSocket (Hermes Protocol)
                    ┌────┴────┐
                    │ Hermes  │
                    └─────────┘
```

## Key Features

- **Signal Plotter**: Drag-and-drop signal visualization with ImPlot
- **3D World View**: Vehicle position/attitude on Earth (osgEarth)
- **Topology View**: Module wiring diagram (imgui-node-editor)
- **Console**: Event stream, phase transitions, command history
- **Inspect Mode**: Shadow execution for debugging without instrumentation

## Tech Stack

| Layer | Library | Purpose |
|:------|:--------|:--------|
| UI Framework | ImGui Bundle | Dear ImGui + Hello ImGui + ImPlot + imgui-node-editor |
| Plotting | ImPlot | High-frequency signal visualization |
| Topology | imgui-node-editor | Block diagram visualization |
| 3D World | TBD (Phase 5) | Geospatial rendering |
| Networking | IXWebSocket | Hermes protocol client with auto-reconnect |
| Data | nlohmann_json | JSON parsing for control channel |

## Quick Start

```bash
# Enter development environment
./scripts/dev.sh

# Build
./scripts/build.sh

# Run (connect to a running Hermes instance)
./build/daedalus --host localhost --port 8765

# Run tests
./scripts/test.sh
```

## Development

All scripts auto-enter the Nix environment if needed:

```bash
./scripts/dev.sh          # Enter Nix development environment
./scripts/build.sh        # Build the project
./scripts/test.sh         # Run all tests
./scripts/ci.sh           # Full CI (build + test)
./scripts/coverage.sh     # Generate coverage report
./scripts/clean.sh        # Clean build artifacts
./scripts/generate_docs.sh # Generate Doxygen docs
./scripts/install-hooks.sh # Install pre-commit hooks
```

## License

MIT
