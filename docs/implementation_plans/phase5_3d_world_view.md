# Phase 5: 3D World View — Implementation Plan

> **Status**: Planning
> **Branch**: `phase5`
> **Estimated LOC**: ~3,500–5,500 new C++ + GLSL + Nix
> **Phases**: 5a (Foundation) → 5b (Vehicle Viz) → 5c (Trails/Effects) → 5d (Tile Streaming) → 5e (Camera Modes) → 5f (glTF Models)

---

## Table of Contents

1. [Design Decisions](#1-design-decisions)
2. [Architecture Overview](#2-architecture-overview)
3. [Technology Stack](#3-technology-stack)
4. [File Structure](#4-file-structure)
5. [Phase 5a — Filament Foundation + Basic Globe](#5-phase-5a--filament-foundation--basic-globe)
6. [Phase 5b — Vehicle Visualization](#6-phase-5b--vehicle-visualization)
7. [Phase 5c — Trails and Effects](#7-phase-5c--trails-and-effects)
8. [Phase 5d — Tile Streaming](#8-phase-5d--tile-streaming)
9. [Phase 5e — Camera Modes](#9-phase-5e--camera-modes)
10. [Phase 5f — glTF Vehicle Models (Future)](#10-phase-5f--gltf-vehicle-models-future)
11. [Signal Binding Reference](#11-signal-binding-reference)
12. [Coordinate Math Reference](#12-coordinate-math-reference)
13. [Risk Register](#13-risk-register)
14. [Testing Strategy](#14-testing-strategy)
15. [Dependencies Summary](#15-dependencies-summary)

---

## 1. Design Decisions

These decisions were evaluated against 7 technology options (Custom OpenGL, Google Filament, bgfx, Ogre3D, VTK, osgEarth, cesium-native) and selected based on rendering quality, build complexity, ImGui integration, WASM future, and Nix packaging feasibility.

| Decision | Choice | Rationale |
|:---------|:-------|:----------|
| **3D Renderer** | Google Filament | PBR rendering, proper lighting/shadows for vehicle models. Apache 2.0. Official WebGL2/WASM support. Shared GL context integration with ImGui is proven. |
| **Tile streaming** | Custom quadtree (NOT cesium-native) | cesium-native has ~24 vcpkg deps, no Nix package, no standalone Filament integration exists. Custom quadtree is ~800-1300 LOC and well-understood. |
| **Tile source** | Natural Earth | Public domain, pre-tiled, up to zoom ~6 (~10km/px). Clean cartographic style fits mission control aesthetic. No API key required. |
| **Globe phasing** | Blue Marble first → tiles later | Ship fast with single 10800x5400 texture, add quadtree LOD in Phase 5d. |
| **Blue Marble asset** | Nix fetchurl derivation | SHA256-pinned, Cachix-cached, available in dev shell. No binary in git. |
| **Vehicle model** | Procedural first → glTF later | Get coordinate math right with a cone/arrow. Add fastgltf model loading in Phase 5f. |
| **Math library** | glm via nixpkgs | Standard OpenGL math (vec3, mat4, quat). `find_package(glm REQUIRED)`. |
| **Filament build** | Custom Nix derivation | Like imgui_bundle — standalone package, Cachix-cached, reproducible. |
| **Terrain elevation** | Deferred | Not needed for MVP. Can add Terrarium RGB tiles (free, AWS) when tile streaming is in place. |

### Options Eliminated

| Option | Why Eliminated |
|:-------|:---------------|
| **cesium-native** | vcpkg with ~24 deps, Nix packaging extremely hard, no Filament backend exists (pioneer work), overkill for our tile streaming needs |
| **osgEarth** | LGPL license, massive dep tree (OSG+GDAL+PROJ), no WASM, not in nixpkgs |
| **bgfx** | Replaces ImGui's OpenGL backend — architectural conflict with Hello ImGui |
| **Ogre3D** | Full game engine with no geospatial features |
| **VTK** | Scientific viz toolkit, absurdly heavy for a globe |
| **Raw OpenGL** | Works but lacks PBR quality for vehicle models, no shadows/environment maps |

---

## 2. Architecture Overview

### Rendering Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│ Hello ImGui Frame (Main Thread, OpenGL 3.2 Core)                │
│                                                                   │
│  ┌──────────────────────┐  ┌────────────────────────────────┐   │
│  │ ImGui Widgets         │  │ Filament 3D Scene               │   │
│  │ (plotter, console,   │  │ (shared GL context)             │   │
│  │  topology, inspector)│  │                                  │   │
│  │                       │  │  ┌─────────┐  ┌─────────────┐ │   │
│  │                       │  │  │ Globe   │  │ Vehicle     │ │   │
│  │                       │  │  │ (sphere │  │ (cone/model,│ │   │
│  │                       │  │  │  + tex) │  │  attitude)  │ │   │
│  │                       │  │  └─────────┘  └─────────────┘ │   │
│  │                       │  │  ┌─────────┐  ┌─────────────┐ │   │
│  │                       │  │  │ Trail   │  │ FOV Cones   │ │   │
│  │                       │  │  │ (VBO    │  │ (α-blended) │ │   │
│  │                       │  │  │  ring)  │  │             │ │   │
│  │                       │  │  └─────────┘  └─────────────┘ │   │
│  │                       │  │                                  │   │
│  │                       │  │  Renders → offscreen texture    │   │
│  │                       │  └────────────┬───────────────────┘   │
│  │                       │               │                        │
│  │  ImGui::Image(tex) ←─┼───────────────┘                        │
│  │  in "3D World" panel  │                                        │
│  └──────────────────────┘                                         │
└─────────────────────────────────────────────────────────────────┘
```

### Threading Model (Unchanged)

The existing threading model is preserved. The 3D World View runs entirely on the render thread (like all other views). Tile downloading (Phase 5d) uses a background thread with a queue, same pattern as IXWebSocket.

```
Network Thread (IXWebSocket)     Tile Download Thread (Phase 5d)
  │                                │
  │ SPSC: TelemetryQueue          │ SPSC: TileQueue
  │ SPSC: EventQueue              │
  ▼                                ▼
Render Thread (Hello ImGui + Filament)
  ├─ process_telemetry()  →  update SignalBuffers
  ├─ process_events()     →  update state
  ├─ process_tiles()      →  upload textures (Phase 5d)
  ├─ update_world_view()  →  vehicle position, trail, camera
  ├─ Filament render      →  offscreen texture
  └─ ImGui render         →  display all panels including 3D
```

### Integration with Existing Code

The WorldView class follows the same pattern as PlotManager, TopologyView, ConsoleView:

```cpp
// In app.cpp, register as a dockable window:
runner_params.dockingParams.dockableWindows.push_back(
    HelloImGui::DockableWindow{
        "3D World", "MainDockSpace", [this]() { world_view_.render(); }
    });

// In the per-frame update:
void App::update() {
    process_events();
    process_telemetry();
    world_view_.update(signal_buffers_, signal_tree_);  // NEW
}
```

---

## 3. Technology Stack

### New Dependencies (Phase 5)

| Library | Version | Purpose | Acquisition | Phase |
|:--------|:--------|:--------|:------------|:------|
| **Google Filament** | Latest stable | PBR 3D rendering engine | Custom Nix derivation | 5a |
| **glm** | 1.0+ | Math library (vec3, mat4, quat) | nixpkgs (`find_package`) | 5a |
| **stb_image** | Latest | Image loading (Blue Marble PNG/JPEG) | Header-only, bundled or FetchContent | 5a |
| **fastgltf** | v0.9+ | glTF 2.0 model loader | CMake FetchContent | 5f |

### Why Filament?

Filament is Google's physically-based rendering (PBR) engine. Key properties:

- **Apache 2.0** license
- **PBR materials**: Metallic-roughness workflow, environment lighting, shadows
- **Shared GL context**: Proven integration with GLFW + ImGui via `Engine::Builder::sharedContext()`
- **Offscreen rendering**: `RenderTarget` API for FBO rendering → `ImGui::Image()`
- **CMake build**: Compatible with our build system
- **WASM**: Official WebGL2 support via Emscripten (Phase 6)
- **Active**: Google-backed, 18k+ GitHub stars

### Filament + Hello ImGui Integration Pattern

The critical integration step: sharing the OpenGL context between Hello ImGui and Filament.

```cpp
// 1. Hello ImGui creates GLFW window + OpenGL 3.2 context (automatic)
// 2. We get the native GL context handle:
//    Linux/X11:    GLXContext ctx = glfwGetGLXContext(window);
//    Linux/EGL:    EGLContext ctx = glfwGetEGLContext(window);
// 3. Create Filament engine with shared context:
//    auto engine = Engine::Builder()
//        .backend(Engine::Backend::OPENGL)
//        .sharedContext(ctx)
//        .build();
// 4. Create offscreen RenderTarget:
//    auto rt = RenderTarget::Builder()
//        .texture(RenderTarget::AttachmentPoint::COLOR, color_texture)
//        .texture(RenderTarget::AttachmentPoint::DEPTH, depth_texture)
//        .build(*engine);
// 5. Each frame: engine renders to rt → ImGui::Image(color_texture_gl_id)
```

**Critical constraint**: The main context must NOT be current when `Engine::create()` is called. Sequence:
1. `glfwMakeContextCurrent(nullptr)` — release main context
2. Create Filament engine with shared context
3. `glfwMakeContextCurrent(window)` — restore main context

---

## 4. File Structure

```
include/daedalus/
  world/
    world_view.hpp          # Top-level view (owns Filament scene, integrates all components)
    globe_renderer.hpp      # Globe mesh generation + rendering
    vehicle_renderer.hpp    # Procedural vehicle geometry (Phase 5b), glTF later (5f)
    trail_renderer.hpp      # Trajectory trail ring buffer + VBO
    effect_renderer.hpp     # FOV cones, LOS vectors, ground track
    camera_controller.hpp   # Arcball, vehicle-follow, free-fly cameras
    coordinate.hpp          # LLA↔ECEF, ENU/NED frames, RTE transforms
    tile_manager.hpp        # Quadtree tile streaming (Phase 5d)

src/daedalus/world/
    world_view.cpp
    globe_renderer.cpp
    vehicle_renderer.cpp
    trail_renderer.cpp
    effect_renderer.cpp
    camera_controller.cpp
    coordinate.cpp
    tile_manager.cpp

assets/shaders/
    globe.vert / globe.frag         # Or Filament .mat material files
    vehicle.vert / vehicle.frag
    trail.vert / trail.frag
    cone.vert / cone.frag

tests/world/
    test_coordinate.cpp      # LLA↔ECEF round-trip, ENU/NED frame correctness
    test_globe_geometry.cpp  # Sphere mesh generation, UV mapping
    test_trail_buffer.cpp    # Ring buffer push/wrap/capacity
    test_tile_quadtree.cpp   # LOD split/merge decisions (Phase 5d)
```

---

## 5. Phase 5a — Filament Foundation + Basic Globe

**Goal**: A rotating, textured Earth visible in a dockable ImGui panel, powered by Filament.

### Step 1: Filament Nix Derivation

Create a custom Nix derivation for Google Filament, following the imgui_bundle pattern.

**Key challenges**:
- Filament uses CMake but has a bundled shader compiler (`matc`) that compiles `.mat` material files
- Requires clang (our stdenv is already `llvmPackages_latest` — compatible)
- Large build (~200MB source, many internal libraries)
- Need to install: libraries, headers, `matc` tool, and CMake config files

**Derivation sketch**:
```nix
filament = stdenv.mkDerivation {
  pname = "filament";
  version = "<pinned-tag>";
  src = fetchFromGitHub {
    owner = "google";
    repo = "filament";
    rev = "<pinned-commit>";
    hash = "sha256-...";
  };
  nativeBuildInputs = [ cmake ninja python3 ];
  buildInputs = [ libGL xorg.libX11 xorg.libXrandr xorg.libXinerama
                   xorg.libXcursor xorg.libXi ];
  cmakeFlags = [
    "-DFILAMENT_SUPPORTS_OPENGL=ON"
    "-DFILAMENT_SUPPORTS_VULKAN=OFF"
    "-DFILAMENT_SUPPORTS_METAL=OFF"
    "-DFILAMENT_BUILD_FILAMAT=ON"   # material compiler
    "-DUSE_STATIC_LIBCXX=OFF"       # use system libc++
  ];
};
```

**Blue Marble texture derivation**:
```nix
blue-marble = fetchurl {
  url = "https://eoimages.gsfc.nasa.gov/images/imagerecords/73000/73909/world.topo.bathy.200412.3x5400x2700.jpg";
  hash = "sha256-...";
};
```

The texture is exposed in the dev shell via an environment variable (e.g., `DAEDALUS_ASSETS_DIR`) and copied into the build tree during configure.

### Step 2: CMake Integration

```cmake
# In CMakeLists.txt:
find_package(filament CONFIG REQUIRED)
find_package(glm REQUIRED)

target_link_libraries(daedalus_lib PUBLIC
    imgui_bundle::imgui_bundle
    ixwebsocket::ixwebsocket
    nlohmann_json::nlohmann_json
    OpenGL::GL
    Threads::Threads
    filament::filament      # NEW
    glm::glm                # NEW
)
```

### Step 3: Shared GL Context Setup

```cpp
// world_view.hpp
class WorldView {
public:
    void init(GLFWwindow* window);  // Called during PostInit callback
    void update(const SignalBufferMap& buffers, const SignalTree& tree);
    void render();                   // Called each frame in dockable window
    void shutdown();

private:
    // Filament objects
    filament::Engine* engine_ = nullptr;
    filament::Renderer* renderer_ = nullptr;
    filament::Scene* scene_ = nullptr;
    filament::View* view_ = nullptr;
    filament::Camera* camera_ = nullptr;
    filament::SwapChain* offscreen_swap_ = nullptr;
    filament::RenderTarget* render_target_ = nullptr;

    // GL texture for ImGui display
    GLuint color_texture_gl_ = 0;
    int viewport_width_ = 0;
    int viewport_height_ = 0;

    // Components
    GlobeRenderer globe_;
    CameraController camera_ctrl_;
    CoordinateSystem coord_;
};
```

### Step 4: Globe Mesh Generation

Generate a **cube-sphere** (6 cube faces, each subdivided and normalized to a sphere). This geometry is superior to a UV sphere because:
- Near-uniform triangle distribution (no polar pinching)
- Each face is a natural quadtree root for Phase 5d tile streaming
- UV mapping per face avoids equirectangular distortion

```cpp
// globe_renderer.hpp
class GlobeRenderer {
public:
    void init(filament::Engine* engine, filament::Scene* scene);
    void set_texture(filament::Texture* tex);

private:
    void generate_cube_sphere(int subdivisions);  // 32x32 per face = 6144 quads

    filament::VertexBuffer* vbuf_ = nullptr;
    filament::IndexBuffer* ibuf_ = nullptr;
    utils::Entity globe_entity_;
    filament::MaterialInstance* material_ = nullptr;
};
```

For Phase 5a, the single Blue Marble equirectangular texture is mapped to the sphere using standard latitude/longitude UV coordinates:
```
u = 0.5 + atan2(z, x) / (2π)
v = 0.5 - asin(y) / π
```

### Step 5: Arcball Camera

```cpp
// camera_controller.hpp
class CameraController {
public:
    enum class Mode { EarthFixed, VehicleFixed, FreeFly };

    void set_mode(Mode mode);
    void handle_mouse_drag(float dx, float dy);
    void handle_scroll(float delta);
    void update(filament::Camera* cam, const glm::dvec3& vehicle_ecef);

private:
    Mode mode_ = Mode::EarthFixed;
    float azimuth_ = 0.0f;      // horizontal orbit angle
    float elevation_ = 30.0f;   // vertical orbit angle (degrees)
    float distance_ = 3.0f;     // in Earth-radii units
    glm::dvec3 target_{0, 0, 0}; // orbit center (globe center or vehicle)
};
```

### Step 6: Coordinate Math

```cpp
// coordinate.hpp
namespace daedalus::coord {

constexpr double WGS84_A  = 6378137.0;            // semi-major axis (m)
constexpr double WGS84_E2 = 6.6943799901377997e-3; // first eccentricity squared

// Geodetic (lat_rad, lon_rad, alt_m) → ECEF (meters)
glm::dvec3 lla_to_ecef(double lat, double lon, double alt);

// ECEF → Geodetic (Vermeille 2004 closed-form)
struct LLA { double lat, lon, alt; };
LLA ecef_to_lla(const glm::dvec3& ecef);

// ENU-to-ECEF rotation matrix at a given lat/lon
glm::dmat3 enu_to_ecef_rotation(double lat, double lon);

// NED-to-body rotation from Euler angles (ZYX: yaw, pitch, roll)
glm::dmat3 euler_zyx_to_dcm(double yaw, double pitch, double roll);

// Full model matrix: position + attitude in ECEF frame
glm::dmat4 vehicle_model_matrix(double lat, double lon, double alt,
                                 double yaw, double pitch, double roll,
                                 double scale);

// Relative-to-Eye: subtract camera ECEF position (double precision)
// then cast to float for GPU submission
glm::mat4 rte_transform(const glm::dmat4& model, const glm::dvec3& camera_ecef);

} // namespace daedalus::coord
```

### Phase 5a Deliverables

- [ ] Filament custom Nix derivation (build, install, CMake config)
- [ ] Blue Marble texture Nix fetchurl derivation
- [ ] `find_package(filament)` and `find_package(glm)` in CMakeLists.txt
- [ ] Shared GL context initialization (Filament + Hello ImGui)
- [ ] FBO render-to-texture → `ImGui::Image()` in dockable "3D World" panel
- [ ] Cube-sphere globe mesh with Blue Marble texture
- [ ] Arcball camera (mouse drag to orbit, scroll to zoom)
- [ ] `coordinate.hpp` — LLA↔ECEF conversion functions
- [ ] Unit tests: coordinate round-trips, sphere mesh vertex count/normals

**Estimated LOC**: ~800–1,200 C++ + ~100 GLSL/material + Nix derivation

---

## 6. Phase 5b — Vehicle Visualization

**Goal**: A procedural vehicle marker at the correct lat/lon/alt with correct attitude, plus velocity vector.

### Step 1: Read Position + Attitude Signals

Wire the WorldView to read from signal buffers using the Icarus signal naming convention:

```cpp
// Signal names from Icarus Vehicle6DOF:
static constexpr auto SIG_LAT   = "rocket.Vehicle.position_lla.lat";   // rad
static constexpr auto SIG_LON   = "rocket.Vehicle.position_lla.lon";   // rad
static constexpr auto SIG_ALT   = "rocket.Vehicle.position_lla.alt";   // m
static constexpr auto SIG_YAW   = "rocket.Vehicle.euler_zyx.yaw";     // rad
static constexpr auto SIG_PITCH = "rocket.Vehicle.euler_zyx.pitch";   // rad
static constexpr auto SIG_ROLL  = "rocket.Vehicle.euler_zyx.roll";    // rad
static constexpr auto SIG_VN    = "rocket.Vehicle.velocity_ned.n";    // m/s
static constexpr auto SIG_VE    = "rocket.Vehicle.velocity_ned.e";    // m/s
static constexpr auto SIG_VD    = "rocket.Vehicle.velocity_ned.d";    // m/s
```

**Auto-discovery**: If these specific signals are not found in the schema, the WorldView gracefully degrades — shows an unzoomed globe with no vehicle marker. This supports connecting to any Hermes simulation, not just Icarus rocket ascent.

### Step 2: Procedural Vehicle Geometry

A simple oriented cone (nose forward) + cylinder (body) rendered at the vehicle position:

```cpp
// vehicle_renderer.hpp
class VehicleRenderer {
public:
    void init(filament::Engine* engine, filament::Scene* scene);
    void update(const glm::dmat4& model_matrix, bool visible);

    // Velocity vector line (optional)
    void set_velocity_vector(const glm::dvec3& vel_ned, const glm::dmat3& enu_to_ecef);

private:
    void generate_cone_mesh(float radius, float height, int sectors);
    utils::Entity vehicle_entity_;
    utils::Entity velocity_entity_;
};
```

The model matrix is computed from the coordinate math:
1. `lla_to_ecef(lat, lon, alt)` → ECEF position
2. `enu_to_ecef_rotation(lat, lon)` → local frame orientation
3. `euler_zyx_to_dcm(yaw, pitch, roll)` → body attitude
4. Compose: `translate(ecef_pos) × R_enu2ecef × R_ned2body^T × scale`
5. Apply RTE: subtract camera ECEF, cast to float

### Step 3: Exaggerated Scale

At global zoom, a 50m rocket is invisible on a 6,371km Earth (ratio 1:127,420). The vehicle scale is dynamically adjusted based on camera distance:

```cpp
double scale = compute_vehicle_scale(camera_distance, vehicle_true_length);
// When camera is far: scale ≈ 1000x (visible dot)
// When camera is close: scale → 1x (true size)
// Smooth interpolation using log(distance)
```

### Phase 5b Deliverables

- [ ] Signal name constants + auto-discovery from schema
- [ ] Vehicle position update from telemetry (LLA → ECEF → model matrix)
- [ ] Procedural cone+cylinder vehicle mesh
- [ ] Attitude visualization (vehicle oriented by yaw/pitch/roll)
- [ ] Velocity vector line (NED velocity in local frame)
- [ ] Dynamic scale based on camera distance
- [ ] Graceful degradation when position signals not available
- [ ] Unit tests: model matrix construction, scale function

**Estimated LOC**: ~400–600 C++

---

## 7. Phase 5c — Trails and Effects

**Goal**: Trajectory trail, FOV cones, LOS vectors, and ground track.

### Step 1: Trajectory Trail

A ring-buffer backed trajectory trail rendered as a colored line strip:

```cpp
// trail_renderer.hpp
class TrailRenderer {
public:
    static constexpr size_t MAX_TRAIL_POINTS = 100000;

    enum class ColorMode { Time, Altitude, Velocity };

    void init(filament::Engine* engine, filament::Scene* scene);
    void push(const glm::dvec3& ecef_pos, float time, float alt, float velocity);
    void set_color_mode(ColorMode mode);
    void update_rte(const glm::dvec3& camera_ecef);  // recompute RTE offsets

private:
    struct TrailVertex {
        glm::vec3 position;  // RTE (relative to eye)
        float time;
        float altitude;
        float velocity;
    };

    std::vector<TrailVertex> ring_buffer_;
    size_t head_ = 0;
    size_t count_ = 0;
    ColorMode color_mode_ = ColorMode::Altitude;
};
```

**Color mapping**: Vertex shader normalizes the selected attribute (time/altitude/velocity) to [0,1], fragment shader applies a colormap (Turbo or Viridis). The colormap is baked into a 1D texture or a shader function.

**Performance**: 100,000 points as GL_LINE_STRIP draws in <1ms on any modern GPU. Single vertex updates via `glBufferSubData` are ~32 bytes per frame — trivial.

### Step 2: FOV Cones

Procedural semi-transparent cone geometry for sensor field-of-view visualization:

```cpp
// effect_renderer.hpp
class EffectRenderer {
public:
    void init(filament::Engine* engine, filament::Scene* scene);

    // FOV cone from a position along a direction
    void add_fov_cone(const std::string& name,
                      float half_angle_rad,
                      float range_m,
                      const glm::vec4& color);  // RGBA, alpha < 1.0

    void update_cone(const std::string& name,
                     const glm::dmat4& transform);  // position + orientation

    // Line-of-sight vector
    void add_los_vector(const std::string& name,
                        const glm::vec4& color);

    void update_los(const std::string& name,
                    const glm::dvec3& from_ecef,
                    const glm::dvec3& to_ecef);

    // Ground track (projection of trajectory onto globe surface)
    void update_ground_track(const std::vector<glm::dvec3>& ecef_positions);
};
```

**Rendering order**: Opaque geometry first (globe, vehicle), then transparent geometry (cones, ground track) with depth writes disabled and alpha blending enabled.

### Step 3: Ground Track

The ground track is the vehicle's trajectory projected onto the Earth surface. For each trail point, normalize the ECEF position to the globe radius:
```cpp
glm::dvec3 ground_pos = glm::normalize(ecef_pos) * WGS84_A;
```

Rendered as a separate line strip on the globe surface, slightly offset outward to avoid z-fighting.

### Phase 5c Deliverables

- [ ] TrailRenderer with ring-buffer VBO
- [ ] Time/altitude/velocity color coding (selectable via ImGui dropdown)
- [ ] FOV cone geometry generation (configurable half-angle, range)
- [ ] Semi-transparent cone rendering (alpha blending)
- [ ] LOS vector rendering (line from sensor to target)
- [ ] Ground track line on globe surface
- [ ] ImGui controls: color mode selector, trail length, cone visibility toggles
- [ ] Unit tests: trail ring buffer push/wrap, cone geometry vertex count

**Estimated LOC**: ~500–800 C++

---

## 8. Phase 5d — Tile Streaming

**Goal**: Replace the single Blue Marble texture with a quadtree-based tile streaming system using Natural Earth tiles.

### Tile Source

**Natural Earth raster tiles** (public domain):
- Pre-tiled in slippy map format (z/x/y.png)
- Available up to zoom ~6 (~10km/pixel) — sufficient for mission control
- Self-hostable or fetchable from public tile servers
- Clean cartographic aesthetic

URL pattern: `https://naturalearthtiles.lukasmartinelli.ch/tiles/natural_earth_2_shaded_relief.raster/{z}/{x}/{y}.png`

### Architecture

```
┌──────────────────────────────────────────────────────────┐
│ TileManager                                                │
│                                                            │
│  ┌────────────────┐    ┌──────────────────────────────┐  │
│  │ 6 Quadtrees    │    │ TileCache                     │  │
│  │ (one per cube  │    │ (LRU, ~200 tiles in GPU mem) │  │
│  │  face)         │    │                                │  │
│  │                │    │ key: (z, x, y)                │  │
│  │ Split/merge    │    │ val: filament::Texture*       │  │
│  │ based on       │    └──────────────────────────────┘  │
│  │ camera dist    │                                        │
│  └───────┬────────┘    ┌──────────────────────────────┐  │
│          │              │ TileDownloader (bg thread)    │  │
│    LOD decisions        │                                │  │
│          │              │ HTTP GET → decode PNG →       │  │
│          ▼              │ enqueue for GPU upload        │  │
│   tilesToRender[]       └──────────────────────────────┘  │
└──────────────────────────────────────────────────────────┘
```

### Quadtree Per Cube Face

Each of the 6 cube faces has its own quadtree. LOD split decision:

```cpp
bool should_split(const TileNode& tile, const glm::dvec3& camera_pos) {
    double tile_size = tile.geometric_error();  // arc-length of tile edge
    double distance = glm::distance(camera_pos, tile.center());
    return (tile_size / distance) > SPLIT_THRESHOLD;  // e.g., 0.5 radians on screen
}
```

### Cube Face → Slippy Map Tile Mapping

The cube-sphere faces need to map to slippy map (z/x/y) tile coordinates. The mapping uses the Mercator projection inverse:

```cpp
// Given a cube face point → lat/lon → tile coordinates
TileCoord face_point_to_tile(int face, float u, float v, int zoom) {
    auto [lat, lon] = face_uv_to_latlon(face, u, v);
    int x = static_cast<int>((lon + 180.0) / 360.0 * (1 << zoom));
    int y = static_cast<int>((1.0 - log(tan(lat * DEG2RAD) +
            1.0 / cos(lat * DEG2RAD)) / M_PI) / 2.0 * (1 << zoom));
    return {zoom, x, y};
}
```

### Async Tile Download

Tiles are fetched on a background thread and uploaded to the GPU on the render thread:

```cpp
// Background thread:
void TileDownloader::fetch(TileCoord coord) {
    std::string url = format_tile_url(coord);
    auto response = http_get(url);  // blocking HTTP
    auto image = decode_png(response.body);
    tile_queue_.push({coord, std::move(image)});  // SPSC queue to render thread
}

// Render thread (each frame):
void TileManager::process_pending_tiles() {
    TileData tile;
    while (tile_queue_.try_pop(tile)) {
        auto* tex = upload_to_filament_texture(tile);
        tile_cache_.insert(tile.coord, tex);
    }
}
```

### Fallback

While child tiles are loading, render the parent tile (lower resolution). This ensures the globe is never partially rendered.

### Phase 5d Deliverables

- [ ] TileManager with 6-face quadtree
- [ ] LOD split/merge based on camera distance
- [ ] Cube face → slippy map coordinate mapping
- [ ] Async HTTP tile downloader (background thread + SPSC queue)
- [ ] PNG tile decoding (stb_image)
- [ ] LRU tile texture cache (~200 tiles)
- [ ] Parent-tile fallback during child loading
- [ ] Natural Earth tile source integration
- [ ] Replace single-texture globe with tiled rendering
- [ ] Unit tests: quadtree split/merge, tile coordinate mapping, LRU cache eviction

**Estimated LOC**: ~800–1,300 C++

---

## 9. Phase 5e — Camera Modes

**Goal**: Three camera modes for different operational perspectives.

### Earth-Fixed (Default)

Arcball orbiting the globe center. Mouse drag rotates azimuth/elevation, scroll zooms distance. This is the standard "Google Earth" interaction.

```cpp
// Already implemented in Phase 5a as the default arcball camera.
// Enhancements:
//   - Double-click to center on a lat/lon
//   - Smooth animation when switching to this mode
```

### Vehicle-Fixed

Camera orbits the vehicle position instead of the globe center. The camera tracks the vehicle as it moves.

```cpp
void CameraController::update_vehicle_fixed(
    filament::Camera* cam,
    const glm::dvec3& vehicle_ecef,
    const glm::dmat3& vehicle_attitude)
{
    target_ = vehicle_ecef;
    // Orbit around vehicle at current azimuth/elevation/distance
    glm::dvec3 offset = compute_orbit_offset(azimuth_, elevation_, distance_);
    // Offset is in local ENU frame, transform to ECEF
    glm::dvec3 eye = vehicle_ecef + enu_to_ecef * offset;
    set_camera(cam, eye, vehicle_ecef);
}
```

### Free-Fly

WASD + mouse-look camera for unconstrained exploration:

```cpp
void CameraController::update_free_fly(
    filament::Camera* cam,
    float dt,
    const InputState& input)
{
    // Mouse look: update pitch/yaw of camera orientation
    // WASD: move forward/backward/left/right relative to camera orientation
    // Q/E: move up/down
    // Shift: speed boost
    position_ += orientation_ * velocity_ * dt;
    set_camera(cam, position_, position_ + forward_);
}
```

### Mode Switching

Smooth animated transitions between modes (interpolate camera position/orientation over ~0.5 seconds):

```cpp
void CameraController::transition_to(Mode new_mode) {
    if (new_mode == mode_) return;
    transition_start_ = current_camera_state();
    transition_end_ = compute_initial_state(new_mode);
    transition_t_ = 0.0f;
    mode_ = new_mode;
}

void CameraController::update(float dt) {
    if (transition_t_ < 1.0f) {
        transition_t_ = std::min(1.0f, transition_t_ + dt / TRANSITION_DURATION);
        float t = smooth_step(transition_t_);  // ease in/out
        current_state_ = lerp(transition_start_, transition_end_, t);
    }
    // ...
}
```

### ImGui Camera Controls

```cpp
// In the 3D World panel:
ImGui::RadioButton("Earth", &mode, 0); ImGui::SameLine();
ImGui::RadioButton("Vehicle", &mode, 1); ImGui::SameLine();
ImGui::RadioButton("Free", &mode, 2);

if (mode == 1 && !vehicle_visible) {
    ImGui::TextColored({1,1,0,1}, "No vehicle position data");
    mode = 0;  // fall back to Earth-fixed
}
```

### Phase 5e Deliverables

- [ ] Three camera modes (Earth-fixed, Vehicle-fixed, Free-fly)
- [ ] Smooth animated transitions between modes
- [ ] Mouse drag + scroll for arcball modes
- [ ] WASD + mouse-look for free-fly mode
- [ ] ImGui radio buttons for mode selection
- [ ] Graceful fallback when vehicle data unavailable
- [ ] Double-click to center on lat/lon (Earth-fixed mode)

**Estimated LOC**: ~200–300 C++

---

## 10. Phase 5f — glTF Vehicle Models (Future)

**Goal**: Replace the procedural cone with a loaded glTF 3D model with articulating parts driven by telemetry.

### Model Loader: fastgltf

```cmake
# CMakeLists.txt (Phase 5f addition):
FetchContent_Declare(
  fastgltf
  GIT_REPOSITORY https://github.com/spnda/fastgltf.git
  GIT_TAG v0.9.0)
FetchContent_MakeAvailable(fastgltf)

target_link_libraries(daedalus_lib PUBLIC fastgltf::fastgltf)
```

**Why fastgltf**: C++17/20, SIMD-accelerated (24x faster than tinygltf), minimal deps (just simdjson), modern API with `std::variant` and `std::optional`, built-in node traversal utilities.

### Articulating Parts Architecture

Each movable part is a separate named node in the glTF hierarchy. Transforms are driven by telemetry signals via a config-driven binding:

```json
// config/vehicle_articulations.json
{
  "bindings": [
    {
      "signal": "rocket.Engine.gimbal_yaw",
      "node": "engine_gimbal_yaw",
      "axis": "y",
      "scale": 1.0,
      "offset": 0.0,
      "min_deg": -8.0,
      "max_deg": 8.0
    },
    {
      "signal": "rocket.Engine.gimbal_pitch",
      "node": "engine_gimbal_pitch",
      "axis": "x",
      "scale": 1.0,
      "offset": 0.0,
      "min_deg": -8.0,
      "max_deg": 8.0
    }
  ]
}
```

### Pipeline

1. Load `.glb` file with fastgltf
2. Convert meshes to Filament vertex/index buffers and PBR materials
3. Build scene graph from glTF node hierarchy
4. Each frame: read telemetry signals, apply rotations to bound nodes, recompute global transforms
5. Submit to Filament for rendering

### Model Sources for Testing

- [NASA 3D Models](https://nasa3d.arc.nasa.gov/models) — free spacecraft models
- Custom simplified rocket model (Blender → glTF export)

### Phase 5f Deliverables

- [ ] fastgltf FetchContent integration
- [ ] glTF → Filament mesh/material conversion
- [ ] Scene graph with named node lookup
- [ ] Config-driven signal-to-node articulation binding
- [ ] Smooth interpolation of articulation values
- [ ] Model selection UI (dropdown or file picker)
- [ ] Unit tests: glTF loading, articulation binding, transform hierarchy

**Estimated LOC**: ~600–1,000 C++

---

## 11. Signal Binding Reference

### Icarus Vehicle6DOF Signal Names

These are the signals exposed by the Icarus rocket simulation via Hermes. All prefixed with `{entity}.Vehicle.` (e.g., `rocket.Vehicle.position_lla.lat`).

| Category | Signal | Units | Frame |
|:---------|:-------|:------|:------|
| **Position (geodetic)** | `position_lla.lat` | rad | WGS84 |
| | `position_lla.lon` | rad | WGS84 |
| | `position_lla.alt` | m | Above WGS84 ellipsoid |
| **Position (ECEF)** | `position.x` | m | ECEF |
| | `position.y` | m | ECEF |
| | `position.z` | m | ECEF |
| **Attitude (quaternion)** | `attitude.w` | — | Body-to-ECEF |
| | `attitude.x` | — | Body-to-ECEF |
| | `attitude.y` | — | Body-to-ECEF |
| | `attitude.z` | — | Body-to-ECEF |
| **Attitude (Euler)** | `euler_zyx.yaw` | rad | ZYX convention |
| | `euler_zyx.pitch` | rad | |
| | `euler_zyx.roll` | rad | |
| **Velocity (body)** | `velocity_body.x` | m/s | Body frame |
| | `velocity_body.y` | m/s | |
| | `velocity_body.z` | m/s | |
| **Velocity (NED)** | `velocity_ned.n` | m/s | Local NED |
| | `velocity_ned.e` | m/s | |
| | `velocity_ned.d` | m/s | |
| **Angular velocity** | `omega_body.x` | rad/s | Body frame |
| | `omega_body.y` | rad/s | |
| | `omega_body.z` | rad/s | |
| **Engine** | `Engine.thrust` | N | |
| | `Engine.throttle_cmd` | [0-1] | |
| | `FuelTank.fuel_mass` | kg | |

### Signal Auto-Discovery Strategy

The WorldView does not hardcode entity names. Instead, it searches the schema for signals matching known patterns:

```cpp
// Priority 1: Look for position_lla.{lat,lon,alt} in any module
// Priority 2: Look for position.{x,y,z} (ECEF, convert to LLA)
// Priority 3: Look for lat/lon/alt as separate signals
// If none found: show unzoomed globe with no vehicle marker
```

---

## 12. Coordinate Math Reference

### Frame Definitions

| Frame | Axes | Usage |
|:------|:-----|:------|
| **ECEF** | X: 0°lat/0°lon, Y: 0°lat/90°E, Z: North Pole | Primary state frame |
| **LLA** | lat: geodetic (WGS84), lon: east-positive, alt: above ellipsoid | Human-readable position |
| **NED** | N: north, E: east, D: down (local tangent plane) | Velocity, attitude reference |
| **ENU** | E: east, N: north, U: up | OpenGL convention (Y-up) |
| **Body** | X: nose, Y: right, Z: down | Vehicle-fixed |

### Key Conversions

**LLA → ECEF** (WGS84 ellipsoid):
```
N = a / sqrt(1 - e² × sin²(lat))
X = (N + alt) × cos(lat) × cos(lon)
Y = (N + alt) × cos(lat) × sin(lon)
Z = (N × (1 - e²) + alt) × sin(lat)
```

**ENU-to-ECEF rotation** at position (lat, lon):
```
R = [ -sin(lon)        -cos(lon)×sin(lat)    cos(lon)×cos(lat) ]
    [  cos(lon)        -sin(lon)×sin(lat)    sin(lon)×cos(lat) ]
    [  0                cos(lat)              sin(lat)           ]
```

**NED-to-ENU swap**: `E_enu = E_ned, N_enu = N_ned, U_enu = -D_ned`

**Euler ZYX to DCM** (yaw ψ, pitch θ, roll φ):
Standard aerospace 3-2-1 rotation: `R = Rx(φ) × Ry(θ) × Rz(ψ)`

**Relative-to-Eye (RTE)**: For GPU precision, subtract camera ECEF position in double precision, then cast to float:
```cpp
glm::mat4 rte = glm::mat4(glm::dmat4(model) - glm::translate(camera_ecef));
```

---

## 13. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| **Filament Nix derivation fails** | Blocks Phase 5a | Medium | Start with FetchContent as fallback. Filament's CMake is standard. Study imgui_bundle derivation as template. |
| **Shared GL context conflict** | Rendering corruption | Medium | Test on X11 and Wayland. Use Filament's Vulkan backend as fallback (if GL sharing fails). |
| **Filament matc shader compiler** | Build complexity | Low | matc is built as part of Filament. Ensure it's available as a build tool in the Nix derivation. |
| **Natural Earth tile server unavailability** | Tile streaming fails | Low | Bundle zoom 0-2 tiles as fallback assets. Support self-hosted tile server. |
| **Precision loss at global scale** | Vehicle placed incorrectly | Low | RTE rendering (subtract camera position in double precision). Well-understood technique. |
| **ImGui mouse capture conflict** | Can't interact with 3D view | Medium | Use `ImGui::IsWindowHovered()` to conditionally route mouse events to camera controller. |
| **Performance with large trail** | Frame rate drop | Low | Trail is capped at 100,000 points. GPU draws 100K lines in <1ms. |
| **GLFW version mismatch** | Filament/Hello ImGui use different GLFW | Medium | Pin GLFW version in Nix. Both libraries support GLFW 3.x. |

---

## 14. Testing Strategy

### Unit Tests (No GPU Required)

| Test File | Coverage |
|:----------|:---------|
| `test_coordinate.cpp` | LLA↔ECEF round-trip (known points: equator, poles, ISS altitude). ENU/NED frame construction. Euler↔DCM. |
| `test_globe_geometry.cpp` | Cube-sphere vertex count, normal direction (all point outward), UV range [0,1]. |
| `test_trail_buffer.cpp` | Ring buffer push, wrap-around, capacity, iteration order after wrap. |
| `test_tile_quadtree.cpp` | Split/merge decisions, tile coordinate mapping, LRU cache eviction. |

### Integration Tests (GPU Required)

- Filament engine creation + offscreen render (produces non-black image)
- FBO texture shared between Filament and ImGui contexts
- Globe renders with correct orientation (North Pole at top, Americas visible at lon=0 camera)

### Manual Validation

- Connect to Hermes (`websocket_telemetry.yaml`): globe visible, no crash
- Connect to Hermes (`icarus_rocket.yaml`): vehicle marker appears at correct position, moves along trajectory
- Orbit camera: smooth, no jitter
- Trail: grows behind vehicle, colors change with altitude

---

## 15. Dependencies Summary

### Current (Phase 1-4)

| Dependency | Source |
|:-----------|:-------|
| imgui_bundle v1.92.5 | Custom Nix derivation |
| IXWebSocket v11.4.6 | CMake FetchContent |
| nlohmann_json | nixpkgs |
| GoogleTest | nixpkgs |
| OpenGL | System (via Nix) |

### Added in Phase 5

| Dependency | Source | Phase |
|:-----------|:-------|:------|
| Google Filament | Custom Nix derivation | 5a |
| glm | nixpkgs | 5a |
| stb_image | Header-only (bundled or FetchContent) | 5a |
| Blue Marble texture | Nix fetchurl | 5a |
| Natural Earth tiles | Runtime download (public CDN) | 5d |
| fastgltf v0.9+ | CMake FetchContent | 5f |

---

## Appendix A: Reference Projects

| Project | Relevance |
|:--------|:----------|
| [filament-glfw-imgui](https://github.com/ambrusc/filament-glfw-imgui) | Shared GL context pattern (Filament + GLFW + ImGui) |
| [filament-with-glfw](https://github.com/roxlu/filament-with-glfw) | Working GLFW + Filament example |
| [3DGlobeRenderer](https://github.com/berkbavas/3DGlobeRenderer) | Minimal C++/OpenGL globe (reference geometry) |
| [OpenGlobe](https://github.com/virtualglobebook/OpenGlobe) | Virtual globe engine textbook companion |
| [Celestia](https://github.com/CelestiaProject/Celestia) | Open-source space visualization |
| [KeepTrack.space](https://github.com/thkruz/keeptrack.space) | Satellite tracker (FOV cones, orbital trails) |
| [NASA 3D Models](https://nasa3d.arc.nasa.gov/models) | Free spacecraft models for testing |

## Appendix B: Key External References

- [Filament documentation](https://google.github.io/filament/)
- [Filament offscreen rendering](https://stunlock.gg/posts/filament_offscreen_renderering/)
- [FBO in ImGui](https://www.codingwiththomas.com/blog/rendering-an-opengl-framebuffer-into-a-dear-imgui-window)
- [Geodetic to ECEF](https://danceswithcode.net/engineeringnotes/geodetic_to_ecef/geodetic_to_ecef.html)
- [Cozzi & Ring: 3D Engine Design for Virtual Globes](https://www.amazon.com/3D-Engine-Design-Virtual-Globes/dp/1568817118)
- [Natural Earth Tiles](https://klokantech.github.io/naturalearthtiles/)
- [Terrarium on AWS (free elevation)](https://registry.opendata.aws/terrain-tiles/)
- [fastgltf documentation](https://fastgltf.readthedocs.io/)
- [AGI_articulations glTF extension](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Vendor/AGI_articulations/README.md)
