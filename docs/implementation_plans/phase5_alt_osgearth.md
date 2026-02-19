# Phase 5 Alternative: osgEarth Framework Migration

> **Status**: Proposed alternative to [phase5_3d_world_view.md](phase5_3d_world_view.md) (Filament approach)
> **Core idea**: Instead of adding a 3D globe to our ImGui app, port our ImGui panels into osgEarth's framework — getting a full geospatial globe "for free."

---

## Table of Contents

1. [Approach Comparison](#1-approach-comparison)
2. [How osgEarth's ImGui Integration Works](#2-how-osgearths-imgui-integration-works)
3. [What We Gain](#3-what-we-gain)
4. [What We Lose](#4-what-we-lose)
5. [Porting Assessment](#5-porting-assessment)
6. [Architecture](#6-architecture)
7. [Nix Packaging](#7-nix-packaging)
8. [Implementation Phases](#8-implementation-phases)
9. [cesium-native Integration (Optional)](#9-cesium-native-integration-optional)
10. [Risk Register](#10-risk-register)
11. [Decision Matrix: Plan A vs Plan B](#11-decision-matrix-plan-a-vs-plan-b)

---

## 1. Approach Comparison

| | **Plan A: Filament** | **Plan B: osgEarth** |
|:--|:---|:---|
| **Philosophy** | Add a 3D globe **to** our ImGui app | Port our ImGui panels **into** an osgEarth app |
| **Globe rendering** | Custom (sphere mesh + shaders + tile streaming) | Built-in (terrain, imagery, atmosphere, lighting) |
| **Renderer** | Google Filament (PBR) | OpenSceneGraph (mature, OpenGL) |
| **ImGui ownership** | Hello ImGui (ImGui Bundle v1.92.5) | osgEarth's vendored ImGui 1.90.2 (docking branch) |
| **3D view integration** | FBO render-to-texture → ImGui::Image() | 3D scene is the viewport; ImGui overlays on top |
| **New code for globe** | ~2,000–3,500 LOC | ~0 LOC (osgEarth handles it) |
| **Porting cost** | ~0 (existing code untouched) | ~500–700 LOC refactored (app lifecycle only) |
| **WASM (Phase 6)** | Filament has official WebGL2 support | **Dead end** — osgEarth explicitly excludes WASM |
| **License** | Apache 2.0 (Filament) | LGPL-3.0 + static linking exception (osgEarth + OSG) |

---

## 2. How osgEarth's ImGui Integration Works

osgEarth's approach is architecturally elegant. Rather than rendering the 3D scene to an FBO and blitting it into an ImGui panel, osgEarth uses a **viewport-adjustment pattern**:

```
┌─────────────────────────────────────────────────────┐
│ Window                                                │
│  ┌──────────┬──────────────────────────────────────┐ │
│  │ Signals  │                                       │ │
│  │ (ImGui   │    3D Earth Scene                     │ │
│  │  panel,  │    (renders directly to this area)    │ │
│  │  docked  │                                       │ │
│  │  left)   │    Globe + terrain + vehicle +       │ │
│  │          │    trails render via OSG scene graph  │ │
│  ├──────────┤                                       │ │
│  │ Inspector│                                       │ │
│  │ (docked  │                                       │ │
│  │  left    │                                       │ │
│  │  bottom) ├──────────────────────────────────────┤ │
│  │          │ Console (ImGui panel, docked bottom)  │ │
│  └──────────┴──────────────────────────────────────┘ │
│  [Plots] [Topology]  ← tabs in center when docked    │
└─────────────────────────────────────────────────────┘
```

### The Mechanism

1. `ImGui::DockSpaceOverViewport()` creates a full-window dockspace with `PassthruCentralNode` flag
2. ImGui panels dock around the edges (left, bottom, etc.)
3. The central docking node is **transparent** — the 3D scene shows through
4. osgEarth dynamically adjusts the OSG camera viewport to match the central node's position/size
5. The 3D scene renders directly to the screen (no FBO overhead), perfectly fitting the undocked area

### Render Order (Per Frame)

```
osg::Viewer::frame()
  ├─ eventTraversal()         // ImGuiEventHandler translates OSG events → ImGui input
  ├─ updateTraversal()        // Scene graph updates (terrain LOD, tile loading)
  ├─ renderingTraversals()
  │   ├─ PreDrawCallback      // ImGui::NewFrame()
  │   ├─ OSG scene render     // 3D globe renders to adjusted viewport
  │   └─ PostDrawCallback     // All ImGui panels draw → ImGui::Render()
  └─ (swap buffers)
```

### osgEarth's Panel System

```cpp
class ImGuiPanel {
public:
    virtual void draw(osg::RenderInfo& ri) = 0;  // Override this
    virtual void load(const osgEarth::Config&);   // Load settings
    virtual void save(osgEarth::Config&);         // Save settings
    // Helpers: findNode<T>(), view(), camera(), getPointAtMouse()
};

// Registration:
auto* ui = new ImGuiAppEngine(arguments);
ui->add("Daedalus", new MyPanel());  // Menu → Daedalus → MyPanel
viewer.getEventHandlers().push_front(ui);
```

---

## 3. What We Gain

### Globe Rendering (Zero Custom Code)

osgEarth provides everything the bootstrap guide's Phase 5 asks for, out of the box:

| Feature | osgEarth Support |
|:--------|:-----------------|
| 3D Earth globe | WGS84 ellipsoid, configurable terrain resolution |
| Terrain elevation | Multiple providers (TMS, WMTS, quantized mesh) |
| Imagery layers | TMS, WMTS, WMS, MBTiles, GeoTIFF, Bing, Cesium Ion |
| Atmosphere | Built-in sky, sun lighting, horizon haze |
| Coordinate transforms | LLA ↔ ECEF, local tangent planes, map projections |
| Camera manipulation | `EarthManipulator` — orbit, zoom, pan, tilt, fly-to |
| Annotations | Labels, placemarks, polylines, polygons on globe |
| Vector features | GeoJSON, shapefiles, KML rendered on terrain |
| LOD management | Automatic terrain/imagery tile streaming + caching |
| Decluttering | Automatic label/annotation overlap prevention |

### 32 Built-in Debug/Dev Panels

osgEarth ships panels for terrain diagnostics, layer management, camera info, network monitoring, shader inspection, scene graph browsing, texture inspection, and more. All available immediately.

### cesium-native Integration (Optional Add-On)

osgEarth has a first-class cesium-native integration (`CesiumNative3DTilesLayer`) providing:
- Cesium Ion 3D Tiles streaming
- Google Photorealistic 3D Tiles
- Cesium World Terrain
- Raster overlay draping (Bing satellite, etc.)

This is a proven, tested integration — not pioneer work.

### Scene Graph for Vehicle + Effects

OpenSceneGraph's scene graph makes vehicle rendering, trails, and FOV cones straightforward:

```cpp
// Vehicle node in the scene graph
auto vehicle = new osg::MatrixTransform;
vehicle->addChild(load_model("vehicle.osgb"));  // or glTF via osgEarth's tinygltf
scene->addChild(vehicle);

// Update position each frame:
osgEarth::GeoPoint pos(srs, lon_deg, lat_deg, alt_m, ALTMODE_ABSOLUTE);
osg::Matrixd local2world;
pos.createLocalToWorld(local2world);
vehicle->setMatrix(local2world * attitude_rotation);
```

Trails, cones, and vectors are `osg::Geometry` nodes attached to the scene graph.

---

## 4. What We Lose

### WASM Future (Phase 6) — CRITICAL

osgEarth's `vcpkg.json` explicitly excludes WASM:
```json
"supports": "!(x86 | wasm32)"
```

OpenSceneGraph has only experimental community WASM support. **If Phase 6 browser deployment is a hard requirement, osgEarth is a dead end.**

Mitigation options:
- Accept that browser deployment requires a separate WebGL/WebGPU viewer
- Use [osgVerse](https://github.com/xarray/osgverse) (OSG-based, has WASM, but immature)
- Build a lightweight CesiumJS/deck.gl web viewer that connects to the same Hermes WebSocket

### Hello ImGui Features

| Lost Feature | Impact | Mitigation |
|:-------------|:-------|:-----------|
| DPI-aware font scaling | Medium | Manual: load fonts at correct scale, `style.ScaleAllSizes()` |
| Named layout persistence | Medium | Use `ImGui::DockBuilder` API programmatically + `imgui.ini` |
| `ImmApp::Run()` one-call setup | Low | Replace with explicit `osg::Viewer` setup |
| Status bar callback | Low | Implement as docked bottom panel |
| Theme presets | Low | Apply `ImGui::StyleColorsDark()` in startup |
| AddOnsParams (ImPlot init) | Low | Call `ImPlot::CreateContext()` in `onStartup` |

### ImGui Bundle

**Must be abandoned entirely.** osgEarth vendors ImGui 1.90.2 (docking). ImGui Bundle provides ImGui v1.91.x. Linking both causes duplicate symbols. We would:
- Use osgEarth's vendored ImGui 1.90.2
- Compile ImPlot separately against osgEarth's ImGui headers
- Compile imgui-node-editor separately against osgEarth's ImGui headers
- Remove `find_package(imgui_bundle)` from CMake

### SingleThreaded Mode Required

osgEarth's ImGui integration requires `viewer.setThreadingModel(SingleThreaded)`. This means the render thread and update thread share one thread. On modern hardware this is rarely a bottleneck for a visualization app, but it's a constraint.

---

## 5. Porting Assessment

### Code That Needs ZERO Changes (~85% of codebase)

All data layer and protocol code is ImGui-agnostic:

| Component | Files | Why Unchanged |
|:----------|:------|:--------------|
| `SignalBuffer` | `data/signal_buffer.hpp` | Pure data container |
| `SignalTree` | `data/signal_tree.hpp/.cpp` | Tree structure |
| `SPSCQueue` | `data/telemetry_queue.hpp` | Lock-free queue, atomics only |
| `HermesClient` | `protocol/client.hpp/.cpp` | WebSocket + SPSC, no GUI |
| Schema parser | `protocol/schema.hpp/.cpp` | JSON parsing |
| Telemetry decoder | `protocol/telemetry.hpp` | Binary decoding |

### View Rendering Code — Minimal Changes

| View | Current State | Changes Needed |
|:-----|:-------------|:---------------|
| `ConsoleView::render()` | Pure ImGui calls | **None** — wrap in `ImGuiPanel::draw()` |
| `render_playback_controls()` | Pure ImGui calls | **None** — wrap in panel |
| `SignalInspector::render()` | Pure ImGui calls | **None** — wrap in panel |
| Signal tree browser | Pure ImGui calls | **None** — wrap in panel |
| `PlotManager::render()` | ImPlot calls | **None** — just compile ImPlot against osgEarth's ImGui |
| `TopologyView::render()` | imgui-node-editor calls | **None** — compile imgui-node-editor against osgEarth's ImGui |

### Code That Needs Refactoring

| File | Lines | Change |
|:-----|:------|:-------|
| `src/main.cpp` | 6 | **Rewrite**: `osg::Viewer` setup instead of `App::run()` |
| `src/daedalus/app.cpp` | 651 | **Major refactor**: Decompose into `AppState` struct + panel classes |
| `include/daedalus/app.hpp` | 84 | **Major refactor**: Split into `AppState` + panel headers |
| `CMakeLists.txt` | 111 | **Major rewrite**: Replace imgui_bundle with osgEarth deps |

**Estimated porting effort**: ~500–700 lines of new/modified code, plus CMake rework.

---

## 6. Architecture

### New Application Structure

```
src/main.cpp                    // osg::Viewer setup, earth file loading, panel registration
include/daedalus/
    app_state.hpp               // Shared state (buffers, tree, client, topology graph)
    panels/
        signals_panel.hpp       // Signal tree browser (ImGuiPanel subclass)
        plot_panel.hpp          // PlotManager wrapper
        topology_panel.hpp      // TopologyView wrapper
        console_panel.hpp       // ConsoleView wrapper
        inspector_panel.hpp     // SignalInspector wrapper
        controls_panel.hpp      // Playback controls wrapper
        world_panel.hpp         // Vehicle/trail/cone controls for the 3D view
```

### Panel Wrapper Pattern

Each existing view becomes a thin `ImGuiPanel` wrapper:

```cpp
// panels/plot_panel.hpp
class PlotPanel : public osgEarth::ImGuiPanel {
public:
    PlotPanel(AppState& state)
        : ImGuiPanel("Plots"), state_(state) {}

    void draw(osg::RenderInfo& ri) override {
        if (!isVisible()) return;
        ImGui::Begin(name(), visible());
        state_.plot_manager.render_toolbar();
        state_.plot_manager.render(state_.signal_buffers);
        ImGui::End();
    }

private:
    AppState& state_;
};
```

### Main Entry Point

```cpp
// src/main.cpp
int main(int argc, char** argv) {
    osg::ArgumentParser arguments(&argc, argv);
    osgViewer::Viewer viewer(arguments);
    viewer.setThreadingModel(osgViewer::ViewerBase::SingleThreaded);

    // Load earth scene (earth file defines terrain, imagery, sky)
    auto* mapNode = new osgEarth::MapNode();
    mapNode->getMap()->addLayer(new osgEarth::TMSImageLayer(/* Natural Earth */));
    mapNode->getMap()->addLayer(new osgEarth::TMSElevationLayer(/* terrain */));
    viewer.setSceneData(mapNode);

    // Camera manipulator (built-in globe interaction)
    viewer.setCameraManipulator(new osgEarth::EarthManipulator(arguments));

    // Create shared application state
    AppState state;
    state.client = std::make_unique<HermesClient>("ws://127.0.0.1:8765");

    // Register Daedalus ImGui panels
    auto* ui = new osgEarth::ImGuiAppEngine(arguments);
    ui->add("Daedalus", new SignalsPanel(state));
    ui->add("Daedalus", new PlotPanel(state));
    ui->add("Daedalus", new TopologyPanel(state));
    ui->add("Daedalus", new ConsolePanel(state));
    ui->add("Daedalus", new InspectorPanel(state));
    ui->add("Daedalus", new ControlsPanel(state));
    ui->add("Daedalus", new WorldControlsPanel(state));  // vehicle/trail config

    // Startup callback: initialize ImPlot, connect to Hermes
    ui->onStartup = [&state]() {
        ImPlot::CreateContext();
        state.client->connect();
    };

    // Add vehicle/trail nodes to scene graph
    auto* vehicle_node = new osg::MatrixTransform();
    vehicle_node->addChild(create_procedural_vehicle());
    mapNode->addChild(vehicle_node);
    state.vehicle_transform = vehicle_node;

    auto* trail_node = new osg::Geode();
    trail_node->addDrawable(new TrailGeometry());
    mapNode->addChild(trail_node);
    state.trail_geometry = trail_node;

    // Per-frame update callback (drain queues, update vehicle position)
    mapNode->addUpdateCallback(new DaedalusUpdateCallback(state));

    viewer.getEventHandlers().push_front(ui);
    return viewer.run();
}
```

### Queue Draining

The SPSC queue draining (currently in `App::process_events()` and `App::process_telemetry()`) moves to an OSG `UpdateCallback`:

```cpp
class DaedalusUpdateCallback : public osg::NodeCallback {
    AppState& state_;
public:
    void operator()(osg::Node* node, osg::NodeVisitor* nv) override {
        state_.process_events();     // drain EventQueue
        state_.process_telemetry();  // drain TelemetryQueue, update SignalBuffers

        // Update vehicle position in scene graph
        if (state_.has_position()) {
            osgEarth::GeoPoint pos(
                osgEarth::SpatialReference::get("wgs84"),
                state_.lon_deg(), state_.lat_deg(), state_.alt_m(),
                osgEarth::ALTMODE_ABSOLUTE);
            osg::Matrixd local2world;
            pos.createLocalToWorld(local2world);
            state_.vehicle_transform->setMatrix(local2world * state_.attitude_matrix());
        }

        // Append trail point
        state_.trail_geometry->push_point(state_.vehicle_ecef());

        traverse(node, nv);
    }
};
```

---

## 7. Nix Packaging

### Dependency Chain

```
daedalus
  ├── osgEarth (custom Nix derivation)  ← NEW
  │     ├── OpenSceneGraph (nixpkgs)     ✓ already packaged
  │     ├── GDAL (nixpkgs)              ✓ already packaged
  │     ├── PROJ (nixpkgs)              ✓ already packaged
  │     ├── GEOS (nixpkgs)              ✓ already packaged
  │     ├── Protobuf (nixpkgs)          ✓ already packaged
  │     ├── SQLite3 (nixpkgs)           ✓ already packaged
  │     ├── GLEW (nixpkgs)             ✓ already packaged
  │     ├── Draco (nixpkgs)             ✓ already packaged
  │     └── (Dear ImGui vendored inside osgEarth)
  ├── ImPlot (source, compiled against osgEarth's ImGui)
  ├── imgui-node-editor (source, compiled against osgEarth's ImGui)
  ├── IXWebSocket (FetchContent, unchanged)
  ├── nlohmann_json (nixpkgs, unchanged)
  ├── glm (nixpkgs, new)
  └── GoogleTest (nixpkgs, unchanged)
```

### osgEarth Nix Derivation Sketch

```nix
osgearth = stdenv.mkDerivation {
  pname = "osgearth";
  version = "3.7.3";
  src = fetchFromGitHub {
    owner = "gwaldron";
    repo = "osgearth";
    rev = "osgearth-3.7.3";
    hash = "sha256-...";
    fetchSubmodules = true;  # LERC submodule
  };
  nativeBuildInputs = [ cmake pkg-config ];
  buildInputs = [
    openscenegraph gdal proj geos protobuf sqlite glew
    draco spdlog blend2d meshoptimizer libzip libwebp
    curl openssl xorg.libX11
  ];
  cmakeFlags = [
    "-DOSGEARTH_BUILD_IMGUI_NODEKIT=ON"     # Keep ImGui integration
    "-DOSGEARTH_BUILD_CESIUM_NODEKIT=OFF"    # Skip cesium-native (for now)
    "-DOSGEARTH_BUILD_TOOLS=OFF"
    "-DOSGEARTH_BUILD_EXAMPLES=OFF"
    "-DOSGEARTH_BUILD_TESTS=OFF"
    "-DOSGEARTH_BUILD_SHARED_LIBS=ON"        # Dynamic linking (LGPL-clean)
  ];
};
```

**Estimated effort**: Moderate. All direct deps are in nixpkgs. Main risk is osgEarth's CMake finding everything correctly. Comparable to imgui_bundle derivation but with more transitive deps (through GDAL).

### ImGui Conflict Resolution

osgEarth vendors ImGui 1.90.2 (docking) in `src/osgEarthImGui/`. This **conflicts** with imgui_bundle (ImGui ~1.91.x). Resolution:

**Remove imgui_bundle entirely.** Use osgEarth's vendored ImGui. Compile ImPlot and imgui-node-editor as source files against osgEarth's ImGui headers.

```cmake
# ImPlot: compile against osgEarth's vendored ImGui
add_library(implot STATIC
    external/implot/implot.cpp
    external/implot/implot_items.cpp)
target_include_directories(implot PUBLIC external/implot)
target_include_directories(implot PRIVATE ${OSGEARTH_IMGUI_INCLUDE_DIR})

# imgui-node-editor: same approach
add_library(imgui_node_editor STATIC
    external/imgui-node-editor/imgui_node_editor.cpp
    external/imgui-node-editor/imgui_canvas.cpp
    external/imgui-node-editor/crude_json.cpp)
target_include_directories(imgui_node_editor PUBLIC external/imgui-node-editor)
target_include_directories(imgui_node_editor PRIVATE ${OSGEARTH_IMGUI_INCLUDE_DIR})
```

---

## 8. Implementation Phases

### Phase 5a — osgEarth Foundation + Port

**Goal**: Daedalus running inside osgEarth with all existing panels working and a visible Earth globe.

1. **osgEarth Nix derivation** (with ImGui nodekit enabled)
2. **CMake migration**: Replace imgui_bundle with osgEarth, add ImPlot + imgui-node-editor as source deps
3. **AppState extraction**: Pull shared state out of `App` class into `AppState` struct
4. **Panel wrappers**: Create `ImGuiPanel` subclasses for each existing view
5. **Main rewrite**: `osg::Viewer` setup, panel registration, `EarthManipulator`
6. **Queue draining**: Move to `osg::NodeCallback`
7. **Earth scene**: Basic globe with Natural Earth imagery layer
8. **Verify**: All existing panels render correctly alongside the globe

**Deliverables**:
- [ ] osgEarth custom Nix derivation
- [ ] ImPlot compiled against osgEarth's ImGui
- [ ] imgui-node-editor compiled against osgEarth's ImGui
- [ ] AppState struct + 6 panel wrapper classes
- [ ] osg::Viewer main entry point with EarthManipulator
- [ ] DaedalusUpdateCallback for queue draining
- [ ] Natural Earth imagery on globe
- [ ] All existing views functional (plotter, topology, console, inspector, controls)
- [ ] All existing tests passing (data layer tests are unaffected)

**Estimated LOC**: ~500–700 new, ~200 modified, ~100 deleted
**Risk**: ImPlot/imgui-node-editor version compatibility with osgEarth's ImGui 1.90.2

### Phase 5b — Vehicle Visualization

**Goal**: Vehicle marker on the globe at correct position/attitude.

1. **Vehicle node**: `osg::MatrixTransform` with procedural geometry (cone/arrow) or loaded model
2. **Position from telemetry**: Read `position_lla.{lat,lon,alt}` signals → `GeoPoint` → `createLocalToWorld()`
3. **Attitude**: Read `euler_zyx.{yaw,pitch,roll}` → rotation matrix → compose with local-to-world
4. **Auto-discovery**: Search schema for position signals, graceful degradation if not found
5. **Dynamic scale**: Adjust vehicle size based on camera distance (via `EarthManipulator::getDistance()`)

**Deliverables**:
- [ ] Procedural vehicle geometry (osg::Geometry cone+cylinder)
- [ ] Vehicle positioned from LLA telemetry signals
- [ ] Attitude visualization (correct yaw/pitch/roll)
- [ ] Velocity vector line (osg::Geometry)
- [ ] Dynamic scale based on camera distance
- [ ] Unit tests: GeoPoint construction, attitude matrix

**Estimated LOC**: ~300–500

### Phase 5c — Trails and Effects

**Goal**: Trajectory trail, FOV cones, LOS vectors on the globe.

1. **Trail**: `osg::Geometry` with dynamic vertex buffer, ring-buffer pattern, color-coded by time/altitude/velocity
2. **Ground track**: Trail projected onto terrain surface using `MapNode::getTerrain()->getHeight()`
3. **FOV cones**: `osg::Geometry` with alpha blending (osg::StateSet with `GL_BLEND`)
4. **LOS vectors**: `osg::Geometry` lines from sensor to target
5. **ImGui controls**: WorldControlsPanel for color mode, trail length, cone visibility, etc.

**Deliverables**:
- [ ] Trajectory trail (ring buffer, color-coded)
- [ ] Ground track on terrain
- [ ] FOV cone geometry (semi-transparent)
- [ ] LOS vector lines
- [ ] WorldControlsPanel (ImGui panel for 3D view settings)
- [ ] Unit tests: trail ring buffer

**Estimated LOC**: ~500–800

### Phase 5d — Camera Modes

**Goal**: Multiple camera perspectives.

osgEarth's `EarthManipulator` already provides globe orbiting. Additional modes:

1. **Earth-fixed** (default): `EarthManipulator` standard mode
2. **Vehicle-fixed**: Set `EarthManipulator` viewpoint to track vehicle position:
   ```cpp
   osgEarth::Viewpoint vp;
   vp.focalPoint() = GeoPoint(srs, lon, lat, alt);
   vp.heading() = 0; vp.pitch() = -45; vp.range() = 5000;
   manipulator->setViewpoint(vp, 1.0);  // 1 second transition
   ```
3. **Free-fly**: Switch to `osgGA::FlightManipulator` or `FirstPersonManipulator`
4. **Smooth transitions**: `EarthManipulator::setViewpoint()` with duration parameter

**Deliverables**:
- [ ] Camera mode selector (ImGui radio buttons)
- [ ] Vehicle tracking mode (EarthManipulator viewpoint follows vehicle)
- [ ] Free-fly mode (FlightManipulator)
- [ ] Smooth transitions between modes

**Estimated LOC**: ~200–300

### Phase 5e — cesium-native Integration (Optional)

**Goal**: High-resolution terrain and imagery via Cesium Ion 3D Tiles.

1. **cesium-native Nix derivation** (hardest part — ~30 vcpkg deps)
2. **Enable osgEarth Cesium nodekit**: `-DOSGEARTH_BUILD_CESIUM_NODEKIT=ON -DCESIUM_NATIVE_DIR=...`
3. **Add Cesium layers** via earth file or code:
   ```cpp
   auto* cesiumLayer = new CesiumNative3DTilesLayer();
   cesiumLayer->setAssetId(1);  // Cesium World Terrain
   cesiumLayer->setToken(getenv("OSGEARTH_CESIUMION_KEY"));
   mapNode->getMap()->addLayer(cesiumLayer);
   ```
4. **Google Photorealistic 3D Tiles** (alternative to Cesium Ion):
   ```cpp
   cesiumLayer->setURL("https://tile.googleapis.com/v1/3dtiles/root.json?key=...");
   ```

**Deliverables**:
- [ ] cesium-native Nix derivation
- [ ] osgEarth built with Cesium nodekit
- [ ] Cesium World Terrain layer
- [ ] Satellite imagery overlay (Bing or Cesium Ion)
- [ ] UI controls for Cesium Ion asset browsing (built-in CesiumIonGUI panel)

**Estimated LOC**: ~100–200 (most is Nix packaging + config)

### Phase 5f — glTF Vehicle Models (Future)

osgEarth bundles **tinygltf** and OpenSceneGraph has an `osgdb_gltf` plugin. glTF models can be loaded directly:

```cpp
auto* vehicle = osgDB::readNodeFile("rocket.glb");
vehicle_transform->addChild(vehicle);
```

Articulating parts via the glTF node hierarchy + runtime transform updates follow the same pattern as Plan A.

---

## 9. cesium-native Integration (Optional)

osgEarth's cesium-native integration is a **proven, tested path** (unlike Filament + cesium-native which has never been done). The `CesiumNative3DTilesLayer` implements cesium-native's `IPrepareRendererResources` using OpenSceneGraph's geometry system.

### What cesium-native Adds

- Cesium World Terrain (quantized mesh, high-resolution elevation)
- Cesium Ion satellite imagery (Bing Maps, Sentinel-2, etc.)
- Google Photorealistic 3D Tiles (photogrammetry buildings)
- 3D building models (OSM Buildings, etc.)

### Nix Packaging Challenge

cesium-native has ~30 dependencies via vcpkg. Many are in nixpkgs, but ~10 are not (asyncplusplus, expected-lite, s2geometry, ktx, earcut-hpp, etc.). This requires either:
1. Custom Nix derivations for each missing dep
2. A "mega-derivation" that builds cesium-native with its bundled vcpkg deps in a fixed-output derivation

**This is the hardest Nix packaging in the project.** It can be deferred — osgEarth works fine without cesium-native using standard TMS/WMTS tile providers.

---

## 10. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| **WASM dead end** | Blocks Phase 6 browser deployment | **Certain** | Accept separate web viewer, or use osgVerse (immature) |
| **ImGui version mismatch** | ImPlot/node-editor API incompatibility with osgEarth's ImGui 1.90.2 | Medium | Test compilation early in Phase 5a. Both libs are generally backward-compatible. |
| **osgEarth Nix derivation** | Build fails in Nix sandbox | Medium | All deps are in nixpkgs. LERC submodule needs fetchSubmodules. Start with devShell testing. |
| **SingleThreaded performance** | Lower frame rate under heavy telemetry | Low | Visualization apps are GPU-bound, not CPU-thread-bound. 60 Hz is achievable. |
| **OpenSceneGraph learning curve** | OSG's scene graph model is different from immediate-mode ImGui | Medium | Vehicle/trail/cone code is straightforward OSG. The research shows clear patterns. |
| **imgui_bundle removal** | Breaking change, can't easily revert | High impact | Do the port on a feature branch. Keep the old main.cpp path behind a CMake flag initially. |
| **LGPL license** | May restrict distribution options | Low | Both osgEarth and OSG have static linking exceptions. Dynamic linking is cleanest. |
| **cesium-native Nix packaging** | Very hard, may not succeed | High | Defer to Phase 5e. osgEarth works without it using TMS/WMTS. |

---

## 11. Decision Matrix: Plan A vs Plan B

| Criterion | Plan A (Filament) | Plan B (osgEarth) | Weight |
|:----------|:--:|:--:|:--:|
| **Globe quality out-of-box** | Low (DIY sphere) | **High** (full GIS stack) | High |
| **Vehicle PBR rendering** | **High** (Filament PBR) | Medium (OSG basic lighting) | Medium |
| **Integration effort** | Medium (new FBO pipeline) | Medium (port lifecycle) | High |
| **New code for globe** | ~2,000–3,500 LOC | **~0 LOC** | High |
| **Porting cost** | **~0** (existing code untouched) | ~500–700 LOC refactored | Medium |
| **WASM future (Phase 6)** | **Yes** (Filament WebGL2) | **No** (dead end) | Medium–High |
| **Nix packaging** | Hard (Filament derivation) | Hard (osgEarth derivation) | Medium |
| **cesium-native access** | Not integrated | **Proven integration** | Medium |
| **ImGui version risk** | None (keep imgui_bundle) | Medium (version mismatch) | Medium |
| **License** | **Apache 2.0** | LGPL-3.0 (with exception) | Low |
| **Terrain/imagery** | Custom tile streaming (DIY) | **Built-in** (TMS/WMTS/WMS/Cesium) | High |
| **Camera/navigation** | Custom arcball (DIY) | **EarthManipulator** (battle-tested) | Medium |
| **Ecosystem maturity** | Filament: 7 years | **OSG: 20+ years, osgEarth: 15+ years** | Medium |
| **Community examples** | None for Filament+globe | **Many** (osgEarth apps) | Medium |

### When to Pick Plan A (Filament)

- WASM browser deployment in Phase 6 is a **hard requirement**
- You want PBR-quality vehicle rendering (metallic surfaces, environment reflections)
- You prefer to keep the current Hello ImGui architecture untouched
- You want Apache 2.0 licensing throughout

### When to Pick Plan B (osgEarth)

- You want a **production-quality globe immediately** with zero globe-rendering code
- You value terrain, imagery, atmosphere, and camera controls out of the box
- cesium-native integration (3D buildings, photogrammetry) is important
- WASM can be handled by a separate lightweight web viewer
- You're comfortable with the lifecycle refactoring (most code unchanged)

---

## Appendix: Earth File Example

osgEarth scenes are configured via XML "earth files":

```xml
<!-- daedalus.earth -->
<Map name="Daedalus Globe">
    <!-- Natural Earth imagery (public domain, no API key) -->
    <TMSImage name="Natural Earth">
        <url>https://naturalearthtiles.lukasmartinelli.ch/tiles/natural_earth_2_shaded_relief.raster/{z}/{x}/{y}.png</url>
    </TMSImage>

    <!-- Optional: Terrain elevation from AWS (free) -->
    <TMSElevation name="Terrain">
        <url>https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png</url>
        <elevation_encoding>terrarium</elevation_encoding>
    </TMSElevation>

    <!-- Sky with atmosphere -->
    <SkySimple hours="12"/>
</Map>
```

This single file gives you a complete Earth with imagery, terrain, sky, and sun lighting — zero C++ code needed for the globe itself.

---

## Appendix: References

- [osgEarth GitHub](https://github.com/gwaldron/osgearth)
- [osgEarth Documentation](https://docs.osgearth.org)
- [osgEarth cesium-native Integration](https://docs.osgearth.org/en/latest/cesium_native.html)
- [osgEarth ImGuiPanel Source](https://github.com/gwaldron/osgearth/blob/master/src/osgEarthImGui/ImGuiPanel)
- [osgEarth ImGuiEventHandler Source](https://github.com/gwaldron/osgearth/blob/master/src/osgEarthImGui/ImGuiEventHandler.cpp)
- [osgEarth osgearth_imgui.cpp](https://github.com/gwaldron/osgearth/blob/master/src/applications/osgearth_imgui/osgearth_imgui.cpp)
- [ImGui Docking Wiki](https://github.com/ocornut/imgui/wiki/Docking)
- [OpenSceneGraph](http://www.openscenegraph.org/)
- [osgVerse (OSG + WASM)](https://github.com/xarray/osgverse)
