# Phase 5 Alternative D: Vector Globe — Raw OpenGL Wireframe

> **Status**: Proposed — recommended over Plans A, B, C for stylistic and practical reasons
> **Branch**: `phase5`
> **Core idea**: Raw OpenGL 3.3 FBO rendering with a vector/wireframe aesthetic. No Filament, no osgEarth, no external process.
> **Aesthetic**: CRT phosphor display — glowing lines on near-black, matching the existing ImGui dark theme.

---

## Table of Contents

1. [Why This Approach](#1-why-this-approach)
2. [Visual Design](#2-visual-design)
3. [Architecture](#3-architecture)
4. [Technology Stack](#4-technology-stack)
5. [File Structure](#5-file-structure)
6. [Phase 5a — FBO Foundation + Graticule Globe](#6-phase-5a--fbo-foundation--graticule-globe)
7. [Phase 5b — Coastlines + Vehicle](#7-phase-5b--coastlines--vehicle)
8. [Phase 5c — Trail + Effects](#8-phase-5c--trail--effects)
9. [Phase 5d — Bloom Glow Pass](#9-phase-5d--bloom-glow-pass)
10. [Phase 5e — Camera Modes + HUD Overlay](#10-phase-5e--camera-modes--hud-overlay)
11. [Coordinate Math Reference](#11-coordinate-math-reference)
12. [Risk Register](#12-risk-register)
13. [Comparison Matrix](#13-comparison-matrix)
14. [Dependencies Summary](#14-dependencies-summary)

---

## 1. Why This Approach

The existing three plans each add a major rendering engine to get a photorealistic globe. But photorealism is not the goal — **information density with visual coherence** is.

| Problem | Plan A (Filament) | Plan B (osgEarth) | Plan C (FlightGear) | **Plan D (Vector)** |
|:--------|:------------------|:------------------|:--------------------|:--------------------|
| OpenGL already in the project | Adds second engine | Replaces imgui_bundle | External process | **Uses what exists** |
| New derivation complexity | ~200MB Nix build | ~300MB Nix build | None needed | **None needed** |
| Visual coherence with ImGui UI | Low (PBR≠terminal) | Low (photorealistic) | None (separate window) | **High (same language)** |
| WASM (Phase 6) | Yes (WebGL2) | No (dead end) | No (external process) | **Yes (WebGL2, vertex-only shader path)** |
| Lines, trails, cones | Custom | Custom | Limited | **Native (GL_TRIANGLES)** |
| LOC for globe | ~2,000–3,500 | ~500 (port cost) | ~150 | **~1,200–1,800** |

**Key insight**: OpenGL is already linked via `OpenGL::GL` in CMakeLists.txt and GLFW is already present via imgui_bundle. We can open an FBO, draw into it, and display it as `ImGui::Image()` — *without adding a single new library dependency for the rendering.*

The vector aesthetic is not a compromise — it is the right choice for a mission control visualization tool:

- **Information-dense**: A colored lat/lon grid + coastline vectors communicate geographic context without photorealistic distractions.
- **Performance**: 100,000 line segments draw in ~0.3ms. No texture downloads, no tile streaming on day one.
- **Original**: No other flight sim / mission control tool looks like this. It matches the existing ImPlot / topology aesthetic.
- **Maintainable**: Raw OpenGL 3.3 has no API changes in 15 years. No engine upgrade treadmill.

---

## 2. Visual Design

### Aesthetic Reference

The target look is a **CRT phosphor vector display** — the kind used in control rooms and early arcade games (Battlezone, Asteroids, the original Elite). Bright, anti-aliased, glowing lines on near-black. Every element is a line, never a filled polygon.

```
╔══════════════════════════════════════════════════════════════╗
║  3D World                                      [cam: Earth]  ║
║  ┌──────────────────────────────────────────────────────┐    ║
║  │                    *  *  *                            │    ║
║  │              ·  ·   ╭──────╮   ·  ·                 │    ║
║  │           ·        ╱ EARTH ╲       ·                 │    ║
║  │         ·         │  ══════ │        ·               │    ║
║  │        ·   ───────│────○────│───────  ·              │    ║
║  │         ·         │  ══════ │        ·               │    ║
║  │           ·        ╲       ╱       ·                 │    ║
║  │              ·  ·   ╰──────╯   ·  ·                 │    ║
║  │                                                       │    ║
║  │  LAT: 28.6°N  LON: -80.6°W  ALT: 12,450 m           │    ║
║  │  SPD: 1,240 m/s  ↑ 87°                               │    ║
║  └──────────────────────────────────────────────────────┘    ║
╚══════════════════════════════════════════════════════════════╝
```

### Color Palette

Three named themes (switchable at runtime via ImGui combo):

#### Theme: Phosphor (default)
```
Background:    #000000   (true black — all glow reads against it cleanly)
Dim graticule: #005522   (HDR 0.4 — barely visible, 30° intervals)
Bright graticule: #007733 (HDR 0.8 — 15° grid)
Special lines: #00ff66   (HDR 3.5 — equator, prime meridian, tropics)
Coastlines:    #00cc44   (HDR 2.5 — phosphor green, dominant earth color)
Vehicle:       #ffaa00   (HDR 4.0 — amber, matches topology node wires)
Trail recent:  #00ff66   (HDR 3.0 → newest trail point, high bloom)
Trail old:     #002211   (HDR 0.3 → oldest trail point, barely visible)
Velocity vec:  #f4a520   (HDR 2.0 — amber dashed line)
FOV cone edge: #006633   (HDR 1.2 — dim green wireframe cone)
HUD text:      #a0e8f0   (light cyan via ImGui DrawList — no HDR needed)
```

HDR values above 1.0 drive the bloom halo. The vehicle at 4.0 produces a dramatic glow; graticule at 0.4 stays subtle. These are `vec3` multipliers in the fragment shader (`gl_FragColor = vec4(u_color * u_hdr_intensity, alpha)`).

Reference aesthetic: [mmcloughlin/globe](https://github.com/mmcloughlin/globe) — Go software renderer with this exact phosphor palette.

#### Theme: Amber (warm)
```
Background:    #0a0800
Graticule:     #1a0800
Coastlines:    #c87800
Vehicle:       #ffe060
Trail:         #804000 → #ffa040
Velocity vec:  #ff4040
HUD text:      #c09040
```

#### Theme: Night (cold tactical)
```
Background:    #020508
Graticule:     #040d18
Coastlines:    #6080b0
Vehicle:       #d0e0ff
Trail:         #1a2040 → #4060c0
HUD text:      #8090b0
```

### Line Rendering Style

All geometry is lines. The "phosphor glow" comes from two techniques stacked:

1. **CPU quad tessellation (primary path, WASM-compatible)** — each line segment is expanded to a screen-aligned quad *on the CPU at init time* for static geometry (graticule, coastlines). Dynamic geometry (trail) expands each new segment on push. This requires only vertex + fragment shaders and works identically on desktop GL 3.3 and WebGL2 (no geometry shader stage exists in WebGL):
   ```glsl
   // Fragment: smooth line edge with glow core
   in float v_dist;  // [-1, 1] from line center, set by CPU tessellation
   float alpha = exp(-v_dist * v_dist * u_soft_edge) * u_color.a;
   frag_color = vec4(u_color.rgb, alpha);
   ```

2. **Optional bloom pass** — a 2-pass Gaussian blur of the bright regions, composited back, creating the "light bleeding" halo seen on CRT displays. Adds ~0.5ms and can be toggled.

> **Why not geometry shaders?** OpenGL 3.3 core requires GS support, but WebGL (Phase 6) has only vertex + fragment stages. `glLineWidth > 1.0` is also not guaranteed in core profile. CPU tessellation is the only approach that works unchanged on all three targets: desktop GL, future WebGL2, and CI headless. Geometry shaders are a desktop-only optimization that can be added later as an optional path.

Line widths by semantic role:
```
Dim graticule: 0.8px screen-space
Major graticule: 1.2px
Coastlines:   1.5px (with soft edge, total visual width ~3px)
Vehicle body: 2.0px
Trail:        1.5px → 2.5px (thickens toward newest point)
Velocity vec: 1.5px with dashed pattern
Equator/dateline: 1.5px (highlighted)
```

---

## 3. Architecture

### Rendering Pipeline

```
Hello ImGui frame (render thread)
  │
  ├─ process_telemetry()      ← drain SPSC queue, update SignalBuffers
  ├─ world_view_.update()     ← read signal buffers, update vehicle state
  │
  ├─ world_view_.render()     ← called inside dockable "3D World" panel
  │     │
  │     ├─ [if viewport changed] recreate FBO
  │     │
  │     ├─ glBindFramebuffer(GL_FRAMEBUFFER, fbo_)
  │     │
  │     ├─ Pass 1: Scene render
  │     │     ├─ draw_background()         (clear to bg color)
  │     │     ├─ globe_.draw_graticule()   (lat/lon grid)
  │     │     ├─ globe_.draw_coastlines()  (vector coast lines)
  │     │     ├─ trail_.draw()             (vehicle trajectory)
  │     │     ├─ vehicle_.draw()           (cone marker)
  │     │     ├─ effects_.draw()           (FOV cones, LOS vectors)
  │     │     └─ camera_ctrl_.apply()      (update view/proj matrices)
  │     │
  │     ├─ Pass 2 (optional): Bloom
  │     │     ├─ downsample FBO → quarter-res FBO
  │     │     ├─ horizontal Gaussian blur
  │     │     ├─ vertical Gaussian blur
  │     │     └─ composite: scene + bloom*intensity
  │     │
  │     ├─ glBindFramebuffer(GL_FRAMEBUFFER, 0)
  │     │
  │     └─ ImGui::Image(fbo_color_texture_,
  │                     ImGui::GetContentRegionAvail())
  │
  └─ HUD overlay (ImGui::GetWindowDrawList())
        ├─ lat/lon/alt readout
        ├─ speed + heading
        └─ camera mode indicator
```

### No Engine, No Context Sharing

Unlike Plan A (Filament), we do **not** need to share an OpenGL context between two engines. We use the *same* OpenGL context that Hello ImGui already owns. The pattern:

```cpp
// Before WorldView renders:
// Hello ImGui is already rendering with OpenGL. We just bind our FBO
// and render into it. Hello ImGui's OpenGL state is preserved.

void WorldView::render() {
    // Save Hello ImGui's GL state
    GLint prev_fbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);

    // Render our scene to our FBO
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glViewport(0, 0, width_, height_);
    draw_scene();

    // Restore Hello ImGui's state
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);

    // Display result as an ImGui image
    ImGui::Image((ImTextureID)(intptr_t)color_tex_, panel_size);
}
```

This is vastly simpler than the Filament shared context initialization sequence.

---

## 4. Technology Stack

### New Dependencies

| Library | Purpose | Source | Notes |
|:--------|:--------|:-------|:------|
| **glm** | Math (vec3, mat4, quat) | nixpkgs | Already decided in Phase 5 plan |
| **None else** | — | — | OpenGL + GLFW already present |

### What We Already Have

| Resource | Status |
|:---------|:-------|
| `OpenGL::GL` | Already in CMakeLists.txt |
| `glfw` | Via imgui_bundle Nix derivation |
| `GL/gl.h` headers | Via `libGL` in buildInputs |
| GLFW window handle | Accessible via `glfwGetCurrentContext()` |

### Coastline Data

**Use Natural Earth 50m GeoJSON loaded with nlohmann_json** — already a project dependency. No shapefile library, no Python bake tool, no binary assets.

Natural Earth publishes pre-converted GeoJSON at [nvkelso/natural-earth-vector](https://github.com/nvkelso/natural-earth-vector):
- `ne_50m_coastline.geojson` — ~1MB, ~60K coordinate pairs, 300+ line strips
- `ne_110m_coastline.geojson` — ~250KB, ~10K coordinate pairs (too coarse for zoom)
- `ne_10m_coastline.geojson` — ~5.5MB, ~200K coordinate pairs (high detail, optional)

**50m is the right choice**: clean continent-level coastlines without tiny islands, sufficient for any orbital altitude.

**Loading approach** — the GeoJSON is `FeatureCollection` with `LineString` / `MultiLineString` geometries, `[lon, lat]` per RFC 7946:

```cpp
// In globe_renderer.cpp
std::vector<LineStrip> load_coastlines_geojson(const std::string& path) {
    std::ifstream f(path);
    auto j = nlohmann::json::parse(f);
    std::vector<LineStrip> result;

    auto process = [&](const nlohmann::json& coords) {
        LineStrip strip;
        for (const auto& pt : coords)
            strip.push_back({pt[0].get<float>(), pt[1].get<float>()});  // [lon, lat]
        result.push_back(std::move(strip));
    };

    for (const auto& feat : j["features"]) {
        const auto& geom = feat["geometry"];
        const auto  type = geom["type"].get<std::string>();
        if (type == "LineString")
            process(geom["coordinates"]);
        else if (type == "MultiLineString")
            for (const auto& ls : geom["coordinates"]) process(ls);
    }
    return result;
}
```

The file is exposed via `$DAEDALUS_ASSETS_DIR` (same pattern as other assets) set in the Nix dev shell. For CI/offline use, bundle it as a Nix `fetchurl` derivation (no custom build needed — just a hash-pinned download).

**Recommendation**: Ship the 50m GeoJSON as a Nix fetchurl derivation. Loaded at startup in ~50ms.

---

## 5. File Structure

```
include/daedalus/world/
    world_view.hpp          # Top-level view — owns FBO, dispatches to components
    globe_renderer.hpp      # Graticule + coastline line rendering
    vehicle_renderer.hpp    # Wireframe vehicle marker (cone/arrow)
    trail_renderer.hpp      # Trajectory ring buffer VBO
    effect_renderer.hpp     # FOV cones (wireframe), LOS vectors, ground track
    camera_controller.hpp   # Arcball / vehicle-follow / free-fly
    coordinate.hpp          # LLA↔ECEF, ENU/NED frames, RTE transforms
    bloom_pass.hpp          # Two-pass Gaussian bloom (optional, toggleable)
    gl_util.hpp             # FBO creation, shader compilation helpers
    coastline_data.hpp      # Pre-baked NE 110m coastline vertices (generated)

src/daedalus/world/
    world_view.cpp
    globe_renderer.cpp
    vehicle_renderer.cpp
    trail_renderer.cpp
    effect_renderer.cpp
    camera_controller.cpp
    coordinate.cpp
    bloom_pass.cpp
    gl_util.cpp

assets/shaders/
    line.vert               # Transforms pre-tessellated quad corners to clip space
    line.frag               # Gaussian edge falloff, emissive color, optional colormap
    bloom_down.vert/.frag   # Downsample pass
    bloom_blur.vert/.frag   # Horizontal/vertical Gaussian
    bloom_compose.vert/.frag # Additive composite + Reinhard tone mapping

tests/world/
    test_coordinate.cpp     # LLA↔ECEF round-trip
    test_globe_geometry.cpp # Graticule vertex count, line strip correctness
    test_trail_buffer.cpp   # Ring buffer push/wrap/capacity
    test_gl_util.cpp        # FBO creation helpers (headless not possible — skip)
```

---

## 6. Phase 5a — FBO Foundation + Graticule Globe

**Goal**: A rotating wireframe lat/lon grid visible in a dockable ImGui panel. No textures, no coastlines yet — just the geometry and the pipeline.

### Critical ImGui Integration Detail

Before the FBO pipeline, one non-obvious detail: OpenGL textures have origin at bottom-left; ImGui has origin at top-left. The `ImGui::Image()` UV coordinates must be flipped:

```cpp
// Correct — flips Y so the globe isn't upside-down
ImGui::Image(
    (ImTextureID)(intptr_t)color_tex_,
    panel_size,
    ImVec2(0, 1),   // UV top-left = OpenGL bottom-left
    ImVec2(1, 0)    // UV bottom-right = OpenGL top-right
);
```

Also: the FBO render must **complete before** `ImGui::Render()` is called. In Hello ImGui this is satisfied naturally — call `WorldView::render()` inside the dockable window callback, which runs before `ImGui::Render()`.

### Step 1: gl_util — FBO + Shader Helpers

```cpp
// gl_util.hpp
namespace daedalus::gl {

struct Fbo {
    GLuint fbo = 0;
    GLuint color_tex = 0;
    GLuint depth_rb = 0;
    int width = 0, height = 0;

    static Fbo create(int w, int h);
    void resize(int w, int h);
    void destroy();
};

GLuint compile_shader(GLenum type, const char* src);
GLuint link_program(GLuint vert, GLuint geom, GLuint frag);  // geom optional (0)
GLuint link_program(GLuint vert, GLuint frag);

} // namespace daedalus::gl
```

FBO uses `GL_RGB16F` color attachment for HDR (needed for bloom) and `GL_DEPTH_COMPONENT24` renderbuffer.

### Step 2: Line Shader Pipeline

The core rendering primitive is **CPU-tessellated quads** drawn as `GL_TRIANGLES`. Each line segment produces 4 vertices (2 triangles) with a pre-computed `v_dist` attribute encoding which edge of the quad the vertex is on.

**CPU tessellation helper** (`gl_util.hpp`):
```cpp
struct LineVertex {
    glm::vec3 pos;    // 3D position (unit sphere or RTE-adjusted ECEF)
    float     dist;   // -1.0 = left edge, +1.0 = right edge (pre-set by CPU)
    float     value;  // per-vertex data (trail colormap, 0 for others)
};

// Expand a line segment p0→p1 into 4 quad vertices.
// The actual screen-space perpendicular is computed in the vertex shader;
// dist just tracks which edge so the fragment can apply the glow falloff.
// Call this at init time for static geometry; on push() for trail.
inline void push_segment(std::vector<LineVertex>& verts,
                          glm::vec3 p0, glm::vec3 p1,
                          float value = 0.0f)
{
    // Skip degenerate segments (duplicate points)
    if (glm::distance(p0, p1) < 1e-9f) return;

    // 4 corners: p0-left, p0-right, p1-left, p1-right (→ 2 triangles via index)
    verts.push_back({p0, -1.0f, value});
    verts.push_back({p0, +1.0f, value});
    verts.push_back({p1, -1.0f, value});
    verts.push_back({p1, +1.0f, value});
}
// Corresponding index pattern per segment (add base_offset to each):
// { 0, 1, 2,  1, 3, 2 }  (two triangles, CCW winding)
```

**Vertex shader** (`line.vert`) — computes the screen-space perpendicular offset:
```glsl
#version 330 core
layout(location = 0) in vec3   a_pos;    // 3D corner position
layout(location = 1) in float  a_dist;   // -1 or +1 (edge side)
layout(location = 2) in float  a_value;  // data value (trail colormap)

uniform mat4  u_vp;         // view-projection (RTE-adjusted for vehicles)
uniform vec2  u_viewport;   // viewport in pixels
uniform float u_width;      // line half-width in pixels

out float v_dist;
out float v_value;

void main() {
    // NOTE: the opposite endpoint must be passed somehow to compute the offset.
    // Simplest approach: pack both endpoints in the VBO, waste bandwidth, gain
    // portability. For static geometry, this is uploaded once and never changes.
    // See LineVertex2 struct in gl_util.hpp for the dual-endpoint variant.
    //
    // For Phase 5a, use the simplest possible approach:
    // just pass a_pos through and use GL_LINE_STRIP at width 1px, then add
    // proper tessellation in Phase 5b when the line width matters visually.
    gl_Position = u_vp * vec4(a_pos, 1.0);
    v_dist  = a_dist;
    v_value = a_value;
}
```

> **Practical Phase 5a shortcut**: Start with `GL_LINE_STRIP` (width 1px) for the graticule, which works everywhere. The geometry looks fine at 1px for the dim grid. Add CPU-tessellated quad lines in Phase 5b for coastlines where visible width matters.

**Fragment shader** (`line.frag`):
```glsl
#version 330 core
in float v_dist;
in float v_value;

uniform vec4  u_color;       // RGBA color (RGB can exceed 1.0 for HDR/bloom)
uniform float u_soft_edge;   // Gaussian sharpness: 2.0 = soft, 8.0 = crisp
uniform sampler1D u_colormap; // for trail (bind to 1D colormap texture)
uniform bool  u_use_colormap;

out vec4 frag_color;

void main() {
    float alpha = exp(-v_dist * v_dist * u_soft_edge) * u_color.a;
    vec3  color = u_use_colormap
                  ? texture(u_colormap, v_value).rgb
                  : u_color.rgb;
    frag_color = vec4(color, alpha);
}
```

For the bloom pass, lines are drawn with RGB values > 1.0 in HDR. Standard (non-bloom) lines use values ≤ 1.0.

### Step 3: Globe Geometry — Unit Sphere, Not ECEF

**Important**: the globe geometry is rendered on a **unit sphere** (radius = 1.0), not at real WGS84 ECEF scale (~6.4M meters). This avoids float precision issues entirely for the static geometry. Only the vehicle position (which moves and needs precise ECEF math) uses RTE.

```cpp
// Unit sphere projection — no precision issues, no RTE needed for globe geometry
static glm::vec3 latlon_to_unit_sphere(float lat_rad, float lon_rad) {
    return glm::vec3(
         std::cos(lat_rad) * std::cos(lon_rad),   // X: 0°/0°
         std::sin(lat_rad),                        // Y: North Pole
        -std::cos(lat_rad) * std::sin(lon_rad)    // Z: negate for right-hand convention
    );
}
```

Near/far planes for unit sphere: `near = 0.001`, `far = 10.0`. Z-buffer precision at 24 bits is 1/(2^24 * 0.001/10) ≈ 0.0006 world units — far more than needed.

To prevent z-fighting between graticule and coastlines (both at radius 1.0), render coastlines at `r = 1.001`:
```cpp
// Coastline vertex generation
vec3 v = latlon_to_unit_sphere(lat, lon) * 1.001f;  // 0.1% outward
```

### Step 4: Graticule Generation

Latitude/longitude grid lines, generated at startup:

```cpp
// globe_renderer.cpp
void GlobeRenderer::build_graticule(int step_deg) {
    const int SEG = 360;  // segments per full circle

    // Parallels (constant latitude)
    for (int lat = -90 + step_deg; lat < 90; lat += step_deg) {
        float lat_r = glm::radians((float)lat);
        for (int i = 0; i < SEG; ++i) {
            float lon0 = glm::radians(i       * 360.0f / SEG);
            float lon1 = glm::radians((i + 1) * 360.0f / SEG);
            vertices_.push_back(latlon_to_unit_sphere(lat_r, lon0));
            vertices_.push_back(latlon_to_unit_sphere(lat_r, lon1));
        }
    }
    // Meridians (constant longitude)
    for (int lon = 0; lon < 360; lon += step_deg) {
        float lon_r = glm::radians((float)lon);
        for (int i = 0; i < SEG / 2; ++i) {
            float lat0 = glm::radians(-90.0f + i       * 180.0f / (SEG / 2));
            float lat1 = glm::radians(-90.0f + (i + 1) * 180.0f / (SEG / 2));
            vertices_.push_back(latlon_to_unit_sphere(lat0, lon_r));
            vertices_.push_back(latlon_to_unit_sphere(lat1, lon_r));
        }
    }
    // Special lines drawn at higher HDR brightness (separate VBO)
    build_special_lines();  // equator, prime meridian, tropics, polar circles
}
```

At 15° intervals: 11 parallels × 360 + 24 meridians × 180 ≈ **8,280 line segments** — trivial. Special lines add ~6 more full circles.

### Step 4: Arcball Camera

The arcball camera tracks azimuth, elevation, and distance around the globe center (ECEF origin):

```cpp
// camera_controller.hpp
class CameraController {
public:
    void handle_drag(float dx, float dy);   // called from ImGui input in panel
    void handle_scroll(float delta);
    glm::mat4 view_matrix() const;
    glm::mat4 proj_matrix(float aspect) const;
    glm::dvec3 ecef_position() const;       // camera pos in ECEF (for RTE)

    float azimuth_deg_ = 0.0f;
    float elevation_deg_ = 30.0f;
    float distance_ = 2.5f;    // Earth radii (1.0 = surface, 3.0 = mid-orbit)
};
```

### Step 5: WorldView Integration

```cpp
// In app.cpp — add to dockable windows:
runner_params.dockingParams.dockableWindows.push_back(
    HelloImGui::DockableWindow{
        "3D World", "MainDockSpace",
        [this]() { world_view_.render(); }
    });

// In App::run() PostInit callback:
world_view_.init();

// In per-frame update:
world_view_.update(signal_buffers_, signal_tree_);
```

### Phase 5a Deliverables

- [ ] `gl_util`: FBO creation/resize, shader compilation helpers
- [ ] Line vertex/geometry/fragment shaders with Gaussian edge softening
- [ ] Graticule with dim/bright layers (15° dim, 30° brighter, special lines highlighted)
- [ ] Arcball camera (mouse drag + scroll)
- [ ] `WorldView` class integrated with Hello ImGui docking
- [ ] `ImGui::GetIO()` mouse capture so camera only responds when window is hovered
- [ ] `coordinate.hpp`: `lla_to_ecef`, `ecef_to_lla`, `enu_to_ecef_rotation`, RTE transform
- [ ] Unit tests: coordinate round-trips, graticule vertex count

**Estimated LOC**: ~600–900 C++ + ~80 GLSL

---

## 7. Phase 5b — Coastlines + Vehicle

**Goal**: Natural Earth coastlines as vector lines on the globe, and a wireframe vehicle marker.

### Step 1: Coastline Data — GeoJSON via nlohmann_json

Load Natural Earth 50m GeoJSON at startup using `nlohmann_json` (already a project dependency). No Python tool, no shapefile library, no baking step.

The file `ne_50m_coastline.geojson` (~1MB) is exposed via `$DAEDALUS_ASSETS_DIR` set in the Nix dev shell (same pattern used for other assets). A Nix `fetchurl` derivation SHA256-pins the download.

```cpp
// In globe_renderer.cpp
void GlobeRenderer::load_coastlines(const std::string& geojson_path) {
    std::ifstream f(geojson_path);
    auto j = nlohmann::json::parse(f);

    std::vector<LineVertex> verts;
    std::vector<uint32_t>   idxs;

    auto process_strip = [&](const nlohmann::json& coords) {
        // Note: GeoJSON is [lon, lat] per RFC 7946
        for (size_t i = 0; i + 1 < coords.size(); ++i) {
            float lon0 = glm::radians(coords[i  ][0].get<float>());
            float lat0 = glm::radians(coords[i  ][1].get<float>());
            float lon1 = glm::radians(coords[i+1][0].get<float>());
            float lat1 = glm::radians(coords[i+1][1].get<float>());

            // *** Unit sphere only — NOT lla_to_ecef ***
            // Globe is unit sphere; coastlines must use the same space.
            glm::vec3 p0 = latlon_to_unit_sphere(lat0, lon0) * 1.001f;  // z-offset
            glm::vec3 p1 = latlon_to_unit_sphere(lat1, lon1) * 1.001f;

            uint32_t base = static_cast<uint32_t>(verts.size());
            push_segment(verts, p0, p1);         // 4 LineVertex entries
            idxs.insert(idxs.end(), {            // 2 triangles
                base+0, base+1, base+2,
                base+1, base+3, base+2
            });
        }
    };

    for (const auto& feat : j["features"]) {
        const auto& geom = feat["geometry"];
        const auto  type = geom["type"].get<std::string>();
        if      (type == "LineString")
            process_strip(geom["coordinates"]);
        else if (type == "MultiLineString")
            for (const auto& ls : geom["coordinates"])
                process_strip(ls);
    }

    // Upload to VBO + IBO (uploaded once, drawn every frame)
    upload_static_mesh(coast_vao_, coast_vbo_, coast_ibo_,
                       verts, idxs);
    coast_index_count_ = static_cast<int>(idxs.size());
}
```

**50m data size**: ~300 strips × avg 200 pts × 4 verts × 16 bytes ≈ **~4MB VBO**. Uploaded once at init, never touched again.

### Step 3: Wireframe Vehicle

A minimal orientation indicator: three orthogonal lines (body axes) meeting at the vehicle ECEF position, plus a small diamond/chevron outline:

```cpp
// vehicle_renderer.hpp
class VehicleRenderer {
public:
    void init();
    void update(const glm::dmat4& model_matrix, bool visible);
    void draw(GLuint line_program, const glm::mat4& vp);

private:
    // 6 vertices: ±X, ±Y, ±Z axes in body frame
    // Plus a diamond marker (4 vertices in the forward plane)
    // Scale dynamically: larger when zoomed out, true size when close

    static constexpr float AXIS_LEN = 0.02f;   // fraction of Earth radius
    GLuint vao_, vbo_;
};
```

**Colors for body axes:**
- X (nose/forward): bright white
- Y (right wing): dim cyan
- Z (down): dim cyan

Plus a small "V" chevron in the 2D screen space drawn via `ImGui::GetWindowDrawList()`.

### Phase 5b Deliverables

- [ ] Nix fetchurl derivation for `ne_50m_coastline.geojson`
- [ ] `load_coastlines()` via nlohmann_json → CPU-tessellated VBO upload
- [ ] `VehicleRenderer` with body-axis lines and diamond marker
- [ ] Vehicle position from signal buffers (LLA → ECEF → RTE model matrix)
- [ ] Dynamic scale based on camera distance
- [ ] Graceful degradation when position signals absent (globe shown, no vehicle)
- [ ] Unit tests: coastline vertex parsing, vehicle model matrix

**Estimated LOC**: ~400–600 C++

---

## 8. Phase 5c — Trail + Effects

**Goal**: Trajectory trail with color coding, FOV cones as wireframe, ground track.

### Step 1: Trajectory Trail

Ring-buffer backed, color-coded by altitude or velocity:

```cpp
// trail_renderer.hpp
class TrailRenderer {
public:
    static constexpr size_t MAX_TRAIL_POINTS = 100000;
    // GPU buffer capacity = MAX_TRAIL_POINTS * 4 verts * 6 indices per segment
    enum class ColorMode { Altitude, Velocity, Time };

    void init();   // allocate VBO/IBO with GL_DYNAMIC_DRAW
    void push(glm::vec3 globe_pos, float value);  // globe space (unit sphere)
    void set_color_mode(ColorMode mode);
    void draw(GLuint line_program, const glm::mat4& vp);

private:
    // CPU ring buffer mirrors the GPU buffer
    // Each push() adds one segment (between new point and previous point)
    // = 4 LineVertex + 6 indices uploaded via glBufferSubData at the ring position
    //
    // GPU update strategy:
    //   - Allocate VBO at GL_DYNAMIC_DRAW with full MAX_TRAIL_POINTS * 4 capacity
    //   - On push(): expand new segment → 4 verts at ring offset, upload via:
    //       glBufferSubData(GL_ARRAY_BUFFER, byte_offset, 4 * sizeof(LineVertex), &verts)
    //   - On draw(): draw only the live segment count (no full re-upload)
    //   - On ring wrap (head_ == 0): reupload entire VBO once (happens < 1/100000 frames)
    //
    // Result: 1 glBufferSubData(64 bytes) per frame at 1 Hz telemetry = negligible.

    struct LineVertex { glm::vec3 pos; float dist; float value; };
    std::array<LineVertex, MAX_TRAIL_POINTS * 4> cpu_ring_;  // CPU mirror
    glm::vec3  prev_pos_{};
    size_t     seg_count_  = 0;
    size_t     head_seg_   = 0;
    GLuint     vao_ = 0, vbo_ = 0, ibo_ = 0;
};
```

Trail uses a modified fragment shader that reads a per-vertex `value` and maps it to a color gradient via a 1D lookup texture (Turbo or Viridis colormap, 256 entries):

```glsl
// In trail fragment shader:
uniform sampler1D u_colormap;
in float v_value;  // normalized [0, 1]
...
vec3 trail_color = texture(u_colormap, v_value).rgb;
```

### Step 2: FOV Cone (Wireframe)

A cone outline is 12–16 line segments forming the rim + 4 lines from apex to rim:

```cpp
// Not a filled cone — just the wireframe outline.
// Rendered with semi-transparent alpha (~0.4) and same glow edge shader.
// 4 lines from apex to cardinal rim points + 16-segment circle at the base.
```

The wireframe cone looks better than a filled transparent cone in this aesthetic — it reads as "sensor coverage" without obscuring the globe geometry beneath.

### Step 3: Ground Track

The trajectory projected onto the surface. The trail stores positions in **two spaces**:
- **Globe space** (unit sphere): for rendering alongside graticule/coastlines. Project by normalizing to radius 1.0.
- **ECEF space** (real meters): vehicle position from telemetry, used for RTE vehicle rendering.

The ground track uses globe space:
```cpp
// Vehicle ECEF position (from telemetry) → unit sphere surface
glm::dvec3 ecef = coord::lla_to_ecef(lat_rad, lon_rad, 0.0);  // alt=0: surface
glm::vec3  globe_pt = glm::normalize(glm::vec3(ecef)) * 0.999f;  // slightly inside surface
```

**Never mix the two spaces** — all globe geometry (graticule, coastlines, trail, ground track) uses unit sphere. Only the vehicle marker uses real ECEF with RTE.

Rendered as a separate dimmer trail (same color scale but reduced brightness, to differentiate from the true-altitude trail).

### Phase 5c Deliverables

- [ ] `TrailRenderer` ring buffer with VBO
- [ ] Altitude/velocity/time color coding via 1D colormap texture
- [ ] `EffectRenderer`: wireframe FOV cone, LOS vector line
- [ ] Ground track (surface projection)
- [ ] ImGui controls: color mode selector, trail length, cone visibility

**Estimated LOC**: ~500–700 C++

---

## 9. Phase 5d — Bloom Glow Pass

**Goal**: Add the CRT phosphor glow effect that makes the wireframe aesthetic visually distinctive.

### Two-Pass Gaussian Bloom

```
FBO (full res, GL_RGB16F)
      │
      ▼
Downsample × 0.25 (quarter-size FBO)
      │
      ├─ Horizontal Gaussian blur (5-tap or 9-tap kernel)
      │
      ▼
Vertical Gaussian blur
      │
      ▼
Composite: bloom_fbo + scene_fbo (additive blend, adjustable intensity)
      │
      ▼
ImGui::Image()
```

The bloom is subtle — just enough to create the halo around bright lines. Intensity 0.3–0.6 looks good. A `u_bloom_strength` uniform controls it from ImGui.

```glsl
// bloom_compose.frag
uniform sampler2D u_scene;
uniform sampler2D u_blur;
uniform float u_strength;

void main() {
    vec3 scene = texture(u_scene, v_uv).rgb;
    vec3 bloom = texture(u_blur, v_uv).rgb;
    frag_color = vec4(scene + bloom * u_strength, 1.0);
}
```

The full bloom pass takes ~0.5ms on any modern GPU. It's toggled via an ImGui checkbox.

### Tone Mapping (Optional)

Since lines are drawn in HDR (> 1.0), a simple tone mapper prevents clipping:
```glsl
// Reinhard tone mapping in final composite:
vec3 mapped = scene / (scene + vec3(1.0));
```

### Phase 5d Deliverables

- [ ] `BloomPass` class: downsample FBO, horizontal/vertical blur shaders, composite shader
- [ ] `u_bloom_strength` uniform controlled from ImGui slider
- [ ] Toggle bloom on/off without visual discontinuity (lerp intensity)
- [ ] Tone mapping in composite pass

**Estimated LOC**: ~250–350 C++, ~100 GLSL

---

## 10. Phase 5e — Camera Modes + HUD Overlay

**Goal**: Multiple camera perspectives and an ImGui DrawList HUD with telemetry readouts.

### Camera Modes

```cpp
enum class CameraMode { EarthFixed, VehicleFixed, FreeFly };
```

**Earth-fixed** (Phase 5a default): Arcball orbiting ECEF origin. Mouse drag = orbit, scroll = zoom.

**Vehicle-fixed**: Arcball orbiting the vehicle ECEF position, with the same drag/scroll controls. Tracks vehicle as it moves.

**Free-fly**: WASD + Q/E for movement, mouse drag for look. Position in ECEF.

Mode selector in panel header (radio buttons). Smooth 0.5s interpolation when switching.

### HUD Overlay via ImGui DrawList

After `ImGui::Image(...)`, the ImGui panel's draw list is still accessible. We add HUD readouts without a separate rendering pass:

```cpp
void WorldView::draw_hud() {
    if (!has_vehicle_) return;

    auto* draw = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetItemRectMin();
    float pad = 10.0f;

    const ImU32 cyan  = IM_COL32(0, 200, 212, 220);
    const ImU32 amber = IM_COL32(244, 165, 32, 200);
    const ImU32 dim   = IM_COL32(80, 140, 160, 150);

    // Top-left: coordinate readout
    char buf[128];
    snprintf(buf, sizeof(buf), "LAT %+7.3f  LON %+8.3f  ALT %9.1f m",
             glm::degrees(lat_rad_), glm::degrees(lon_rad_), alt_m_);
    draw->AddText(ImVec2(pos.x + pad, pos.y + pad), cyan, buf);

    snprintf(buf, sizeof(buf), "SPD %7.1f m/s  HDG %5.1f deg",
             speed_ms_, heading_deg_);
    draw->AddText(ImVec2(pos.x + pad, pos.y + pad + 18), amber, buf);

    // Top-right: camera mode
    const char* mode_str = (camera_mode_ == CameraMode::EarthFixed) ? "EARTH" :
                           (camera_mode_ == CameraMode::VehicleFixed) ? "VEHICLE" : "FREE";
    draw->AddText(ImVec2(pos.x + ImGui::GetItemRectSize().x - 80, pos.y + pad), dim, mode_str);

    // Bottom-left: simulation time (if available)
    // ... etc
}
```

This uses the existing ImGui draw list — no extra OpenGL draw calls.

### Phase 5e Deliverables

- [ ] Three camera modes with smooth transitions
- [ ] Mouse capture gated on `ImGui::IsWindowHovered()`
- [ ] HUD overlay: lat/lon/alt, speed, heading, simulation time
- [ ] Camera mode selector (radio buttons or tab strip in panel header)
- [ ] ImGui settings panel: theme picker, bloom toggle/strength, line width scale

---

## 11. Coordinate Math Reference

Same math as Plan A — no change needed. Reproduced here for completeness.

### Frame Definitions

| Frame | Usage |
|:------|:------|
| **ECEF** | Primary rendering frame |
| **LLA** | Human-readable position (WGS84) |
| **NED** | Velocity, attitude reference |
| **Body** | Vehicle-fixed |

### Key Formulas

**LLA → ECEF** (WGS84):
```
N = a / sqrt(1 - e² sin²(lat))
X = (N + alt) cos(lat) cos(lon)
Y = (N + alt) cos(lat) sin(lon)
Z = (N(1-e²) + alt) sin(lat)
```

**ENU-to-ECEF rotation**:
```
R = [ -sin(lon)         -cos(lon)sin(lat)    cos(lon)cos(lat) ]
    [  cos(lon)         -sin(lon)sin(lat)    sin(lon)cos(lat) ]
    [  0                 cos(lat)             sin(lat)         ]
```

**Relative-to-Eye (RTE)** for GPU precision:
```cpp
// Compute in double, cast to float for GPU
glm::mat4 rte_vp = glm::mat4(
    glm::dmat4(proj) * glm::dmat4(view) *
    glm::translate(glm::dmat4(1.0), -camera_ecef_d)
);
```

At typical altitudes (0–2,000 km), the distance from camera to vehicle is <2,000 km, well within `float` precision after RTE subtraction.

---

## 12. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| **ImGui GL state corruption** | Blank/corrupted UI | Medium | `GlStateGuard` RAII in `WorldView::render()` saves and restores all 16 state variables — see Appendix C |
| **Coordinate space mismatch** | Coastlines/vehicle misaligned | Medium | Globe geometry (graticule, coastlines, trail) uses unit sphere; vehicle only uses ECEF with RTE — enforced by distinct `latlon_to_unit_sphere()` vs `lla_to_ecef()` functions |
| **Degenerate line segments** | NaN/inf in vertex data | Low | `push_segment()` guards with `distance(p0,p1) < 1e-9` before expanding to quad |
| **WASM line rendering** | Wide lines broken on WebGL | N/A | Primary path uses CPU-tessellated `GL_TRIANGLES` — works on WebGL2 with no changes |
| **Coastline GeoJSON unavailable offline** | No coastlines at startup | Low | Nix `fetchurl` derivation SHA256-pins download; CI bundles asset |
| **Bloom on integrated GPU** | FPS drop | Low | Bloom is optional (checkbox). Default: off. The scene looks good without it. |
| **Mouse capture conflict** | Can't orbit globe | Medium | `ImGui::IsWindowHovered()` guard before routing mouse events to camera |
| **Trail VBO full re-upload on wrap** | Single frame stutter | Very low | Happens once per 100K points. At 1 Hz telemetry ≈ once per 28 hours. |

---

## 13. Comparison Matrix

| Criterion | Plan A (Filament) | Plan B (osgEarth) | Plan C (FlightGear) | **Plan D (Vector)** |
|:----------|:-----------------|:-----------------|:--------------------|:--------------------|
| **Visual coherence with ImGui** | Low | Low | None | **High** |
| **Aesthetic originality** | Low | Low | None | **High** |
| **Globe out-of-box** | DIY sphere | Full GIS | FG renders it | **DIY wire sphere** |
| **New Nix derivation needed** | Yes (200MB+) | Yes (300MB+) | None | **None** |
| **WASM (Phase 6)** | Yes | No | No | **Yes** |
| **Trail/cones/HUD** | Yes (custom) | Yes (custom) | Partial | **Yes (native)** |
| **LOC for globe** | ~3,000+ | ~500 (port) | ~150 | **~1,200** |
| **Photorealistic globe** | Possible | Yes | Yes | **No (intentionally)** |
| **Terrain elevation** | Deferred | Yes | Yes | **Not needed for vector** |
| **Dependency footprint** | Very large | Very large | External binary | **Minimal** |
| **Build time** | Weeks to stabilize | Weeks to stabilize | Zero | **Minutes** |
| **License** | Apache 2.0 | LGPL-3.0 | GPL-2.0 | **Apache 2.0 (own code)** |
| **Debugging** | Complex (engine internals) | Complex (OSG) | Opaque | **Transparent** |

---

## 14. Dependencies Summary

### New in Phase 5 (Plan D only)

| Dependency | Source | Purpose | Phase |
|:-----------|:-------|:--------|:------|
| **glm** | nixpkgs | Math (vec3, mat4, quat) | 5a |
| **ne_50m_coastline.geojson** | Nix fetchurl (naturalearthdata.com) | Coastline vector data | 5b |

That's it. `nlohmann_json` (already present) loads the GeoJSON. No new libraries needed.

### What We Use from Existing Stack

| Resource | How Used |
|:---------|:---------|
| `OpenGL::GL` | Direct GL calls — FBO, VBO, shaders |
| `glfw` | `glfwGetCurrentContext()` for context validation |
| `imgui_bundle` | `ImGui::Image()`, `ImGui::GetWindowDrawList()` for HUD |
| `nlohmann_json` | Signal binding, settings persistence |
| `ixwebsocket` | Telemetry (unchanged) |

---

## Appendix A: Shader Summary

| Shader | Input | Output |
|:-------|:------|:-------|
| `line.vert` | pre-tessellated quad corner (pos, dist, value) | clip space gl_Position |
| `line.frag` | v_dist, u_color, u_soft_edge, optional colormap | frag color with Gaussian alpha |
| `bloom_down.vert` | full-screen quad UV | passthrough |
| `bloom_blur.frag` | scene texture | blurred (H then V pass) |
| `bloom_compose.frag` | scene + blur | HDR composite |

## Appendix B: Reference Projects

| Project | Lang | Relevance |
|:--------|:-----|:----------|
| [mmcloughlin/globe](https://github.com/mmcloughlin/globe) | Go | **Exact target aesthetic**: phosphor green `#00FF41` coastlines on black — the go-to reference for palette and line style |
| [mhalber/Lines](https://github.com/mhalber/Lines) | C/GLSL | 6 OpenGL 3.3 wide-line implementations with source — use the geometry shader approach |
| [vicrucann/osg-shader-3dlines](https://vicrucann.github.io/tutorials/osg-shader-3dlines/) | C++/GLSL | Geometry shader → screen-space quad expansion, tutorial with full GLSL |
| [OpenGlobe](https://github.com/virtualglobebook/OpenGlobe) | C# | **Most technically relevant**: OpenGL 3.3 core, has wireframe globe, RTE, graticule, coastlines from shapefiles. GLSL shaders at `Source/Scene/Renderables/` are directly reusable. |
| [3DGlobeRenderer](https://github.com/berkbavas/3DGlobeRenderer) | C++ | Minimal C++/OpenGL, reads Natural Earth data, interactive camera — closest to target stack |
| [LearnOpenGL Bloom](https://learnopengl.com/Advanced-Lighting/Bloom) | Tutorial | Definitive 3-FBO ping-pong Gaussian bloom with HDR |
| [LearnOpenGL Geometry Shader](https://learnopengl.com/Advanced-OpenGL/Geometry-Shader) | Tutorial | Definitive geometry shader / line expansion tutorial |
| [KeepTrack.space](https://github.com/thkruz/keeptrack.space) | TypeScript | Satellite tracker with dark globe, orbital trails, FOV cones — analogous visual language |
| [mattdesl/drawing-lines-is-hard](https://mattdesl.svbtle.com/drawing-lines-is-hard) | Article | Deep dive on all line rendering approaches and their tradeoffs |
| [nvkelso/natural-earth-vector](https://github.com/nvkelso/natural-earth-vector) | Data | Canonical Natural Earth GeoJSON source (50m, 110m, 10m coastlines) |
| [Coding With Thomas — FBO + ImGui](https://www.codingwiththomas.com/blog/rendering-an-opengl-framebuffer-into-a-dear-imgui-window) | Tutorial | Exact FBO → `ImGui::Image()` pattern with UV flip |

## Appendix C: OpenGL State Save/Restore Checklist

Failure to fully restore GL state after `WorldView::render()` **breaks imgui_bundle's renderer** (symptoms: blank window, corrupted UI, wrong blend modes). This is the most critical implementation detail.

```cpp
struct GlStateGuard {
    // Saved values
    GLint  fbo, vao, program;
    GLint  active_tex, tex_2d;
    GLint  viewport[4];
    GLint  blend_eq_rgb, blend_eq_a;
    GLint  blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a;
    GLboolean blend, depth_test, depth_write, cull_face, scissor_test;

    GlStateGuard() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING,    &fbo);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,   &vao);
        glGetIntegerv(GL_CURRENT_PROGRAM,        &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE,         &active_tex);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,     &tex_2d);
        glGetIntegerv(GL_VIEWPORT,               viewport);
        glGetIntegerv(GL_BLEND_EQUATION_RGB,     &blend_eq_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA,   &blend_eq_a);
        glGetIntegerv(GL_BLEND_SRC_RGB,          &blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB,          &blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA,        &blend_src_a);
        glGetIntegerv(GL_BLEND_DST_ALPHA,        &blend_dst_a);
        glGetBooleanv(GL_BLEND,                  &blend);
        glGetBooleanv(GL_DEPTH_TEST,             &depth_test);
        glGetBooleanv(GL_DEPTH_WRITEMASK,        &depth_write);
        glGetBooleanv(GL_CULL_FACE,              &cull_face);
        glGetBooleanv(GL_SCISSOR_TEST,           &scissor_test);
    }

    ~GlStateGuard() {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBindVertexArray(vao);
        glUseProgram(program);
        glActiveTexture(active_tex);
        glBindTexture(GL_TEXTURE_2D, tex_2d);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glBlendEquationSeparate(blend_eq_rgb, blend_eq_a);
        glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a);
        (blend       ? glEnable : glDisable)(GL_BLEND);
        (depth_test  ? glEnable : glDisable)(GL_DEPTH_TEST);
        glDepthMask(depth_write);
        (cull_face   ? glEnable : glDisable)(GL_CULL_FACE);
        (scissor_test ? glEnable : glDisable)(GL_SCISSOR_TEST);
    }
};

// Usage in WorldView::render():
void WorldView::render() {
    GlStateGuard guard;  // saves on construction, restores on destruction (RAII)
    // ... all GL work here ...
    ImGui::Image(...);   // outside the guard scope is fine — ImGui uses its own state
}
```

**Crucially also required** (often missed):
- Restore `GL_SCISSOR_TEST` — imgui_bundle uses scissor for widget clipping
- Restore `GL_DEPTH_WRITEMASK` — imgui_bundle expects depth writes enabled
- Restore `GL_VERTEX_ARRAY_BINDING` (VAO) — imgui_bundle has its own VAO for the mesh
- Restore `GL_ACTIVE_TEXTURE` + `GL_TEXTURE_BINDING_2D` — imgui_bundle binds fonts/atlas textures
