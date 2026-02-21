# Phase 5 Alternative: FlightGear External Visualization

> **Status**: Proposed alternative to [phase5_3d_world_view.md](phase5_3d_world_view.md) (Filament) and [phase5_alt_osgearth.md](phase5_alt_osgearth.md) (osgEarth)
> **Core idea**: Run FlightGear as a side-by-side process and stream vehicle state over UDP. Zero rendering code in Daedalus.

---

## Table of Contents

1. [Approach Summary](#1-approach-summary)
2. [How It Works](#2-how-it-works)
3. [FGNetFDM Protocol](#3-fgnetfdm-protocol)
4. [Signal Mapping](#4-signal-mapping)
5. [Implementation](#5-implementation)
6. [FlightGear Setup](#6-flightgear-setup)
7. [File Structure](#7-file-structure)
8. [Phased Rollout](#8-phased-rollout)
9. [Comparison vs Plans A and B](#9-comparison-vs-plans-a-and-b)
10. [Risk Register](#10-risk-register)

---

## 1. Approach Summary

FlightGear is a mature open-source flight simulator built on OpenSceneGraph. It renders a full WGS84 globe at any altitude — including from orbital height — and accepts real-time vehicle position and attitude over a simple UDP protocol called **FGNetFDM**.

The integration is a thin bridge: Daedalus reads telemetry from its existing signal buffers and sends UDP packets to FlightGear at ~30 Hz. FlightGear handles everything else: globe rendering, atmosphere, sun/sky, camera modes, terrain streaming.

```
┌────────────────────────────┐       ┌─────────────────────────────────┐
│ Daedalus (existing window) │       │ FlightGear (side-by-side)        │
│                             │       │                                  │
│  Signal tree                │       │  ┌──────────────────────────┐   │
│  Plotter                    │       │  │                            │   │
│  Topology                   │       │  │   Earth from any altitude  │   │
│  Console                    │       │  │   Atmosphere, sun, clouds  │   │
│                             │       │  │   Vehicle at correct pos   │   │
│  ┌─────────────────────┐   │       │  │   Chase / orbit / cockpit  │   │
│  │ FGBridge            │   │       │  │                            │   │
│  │ (reads signal bufs) │───┼──UDP──▶  └──────────────────────────┘   │
│  │ sends @ 30 Hz       │   │       │                                  │
│  └─────────────────────┘   │       └─────────────────────────────────┘
└────────────────────────────┘
```

**What this gets you:**
- Full 3D globe with WGS84 ellipsoid at any altitude
- Atmosphere scattering, horizon glow, stars from orbit
- Vehicle positioned and oriented correctly in real time
- All of FlightGear's camera modes (chase, orbit, cockpit, tower)
- Terrain streaming (SRTM-derived, available at low altitude)
- ~150 lines of new Daedalus code

**What this does not get you:**
- Trajectory trails (not currently supported by this bridge)
- FOV cones / LOS vectors
- Camera control from within Daedalus

---

## 2. How It Works

FlightGear supports an "external FDM" mode where it accepts vehicle state from an outside source rather than computing its own physics. The relevant flags:

```bash
fgfs \
  --fdm=null \                              # disable internal flight dynamics
  --native-fdm=socket,in,30,,5500,udp \    # receive FDM packets from localhost:5500
  --aircraft=SpaceShipOne \                 # (or any rocket-like model)
  --lat=28.608 --lon=-80.604 \             # initial position (KSC launch pad)
  --altitude=0 \
  --timeofday=noon
```

With `--fdm=null`, FlightGear renders the vehicle wherever the UDP packets say it is. The aircraft model is purely cosmetic — the physics don't run.

FlightGear's coordinate system is WGS84 globally. At 200 km altitude, the globe is rendered from space with correct atmosphere. This is not a hack — it's a supported use case (the SpaceShipOne and X-15 addons use it).

---

## 3. FGNetFDM Protocol

The protocol is a fixed-size binary struct sent over UDP. FlightGear uses **network byte order (big-endian)**; on a little-endian host (x86), all fields must be byte-swapped before sending.

The full struct is defined in `$FLIGHTGEAR_SRC/src/Network/net_fdm.hxx`. The fields we actually need:

```cpp
// net_fdm.hxx (simplified — only fields we set)
#pragma pack(push, 1)
struct FGNetFDM {
    // Header
    uint32_t version;        // Must be 24 (FG_NET_FDM_VERSION)
    uint32_t padding;        // Zero

    // Position
    double   longitude;      // geodetic, radians
    double   latitude;       // geodetic, radians
    double   altitude;       // meters above MSL
    float    agl;            // meters above ground (set to altitude if unknown)
    float    phi;            // roll  (radians)
    float    theta;          // pitch (radians)
    float    psi;            // yaw   (radians)
    float    alpha;          // angle of attack (zero)
    float    beta;           // sideslip (zero)

    // Velocities (we fill what we have, zero the rest)
    float    phidot;         // roll  rate (rad/s)
    float    thetadot;       // pitch rate (rad/s)
    float    psidot;         // yaw   rate (rad/s)
    float    vcas;           // calibrated airspeed (can be zero)
    float    climb_rate;     // ft/s (FG internal unit, convert from m/s)
    float    v_north;        // ft/s north velocity
    float    v_east;         // ft/s east velocity
    float    v_down;         // ft/s down velocity
    float    v_body_u;       // zero
    float    v_body_v;       // zero
    float    v_body_w;       // zero

    // Accelerations (zero)
    float    A_X_pilot;
    float    A_Y_pilot;
    float    A_Z_pilot;

    // Stall (zero)
    float    stall_warning;
    float    slip_deg;

    // Engines (zero all)
    uint32_t num_engines;
    uint32_t eng_state[4];
    float    rpm[4];
    float    fuel_flow[4];
    float    fuel_px[4];
    float    egt[4];
    float    cht[4];
    float    mp_osi[4];
    float    tit[4];
    float    oil_temp[4];
    float    oil_px[4];

    // Fuel tanks (zero)
    uint32_t num_tanks;
    float    fuel_quantity[4];

    // Gear (zero)
    uint32_t num_wheels;
    uint32_t gear_pos[3];
    float    gear_steer[3];
    float    gear_compression[3];

    // Environment
    float    cur_time;       // unix timestamp (float — FG uses for sun position)
    float    warp;           // zero
    float    visibility;     // meters (set to 10000 or actual)

    // Control surfaces (all zero for a rocket)
    float    elevator;
    float    elevator_trim_tab;
    float    left_flap;
    float    right_flap;
    float    left_aileron;
    float    right_aileron;
    float    rudder;
    float    nose_wheel;
    float    speedbrake;
    float    spoilers;
};
#pragma pack(pop)
```

**Byte-swapping**: All fields must be converted to network byte order. For `uint32_t` use `htonl()`, for `float` swap the 4 bytes, for `double` swap the 8 bytes. A helper:

```cpp
static float htonf(float f) {
    uint32_t n;
    std::memcpy(&n, &f, 4);
    n = htonl(n);
    std::memcpy(&f, &n, 4);
    return f;
}

static double htond(double d) {
    uint64_t n;
    std::memcpy(&n, &d, 8);
    n = ((uint64_t)htonl(n & 0xFFFFFFFF) << 32) | htonl(n >> 32);
    std::memcpy(&d, &n, 8);
    return d;
}
```

---

## 4. Signal Mapping

The Icarus rocket signals from Hermes map to FGNetFDM fields as follows:

| FGNetFDM field | Hermes signal | Unit conversion |
|:---------------|:--------------|:----------------|
| `latitude` | `rocket.Vehicle.position_lla.lat` | rad → rad (direct) |
| `longitude` | `rocket.Vehicle.position_lla.lon` | rad → rad (direct) |
| `altitude` | `rocket.Vehicle.position_lla.alt` | m → m (direct) |
| `phi` | `rocket.Vehicle.euler_zyx.roll` | rad → rad (direct) |
| `theta` | `rocket.Vehicle.euler_zyx.pitch` | rad → rad (direct) |
| `psi` | `rocket.Vehicle.euler_zyx.yaw` | rad → rad (direct) |
| `phidot` | `rocket.Vehicle.omega_body.x` | rad/s → rad/s (direct) |
| `thetadot` | `rocket.Vehicle.omega_body.y` | rad/s → rad/s (direct) |
| `psidot` | `rocket.Vehicle.omega_body.z` | rad/s → rad/s (direct) |
| `climb_rate` | `rocket.Vehicle.velocity_ned.d` | m/s → ft/s (× -3.28084, negate for up) |
| `v_north` | `rocket.Vehicle.velocity_ned.n` | m/s → ft/s (× 3.28084) |
| `v_east` | `rocket.Vehicle.velocity_ned.e` | m/s → ft/s (× 3.28084) |
| `v_down` | `rocket.Vehicle.velocity_ned.d` | m/s → ft/s (× 3.28084) |
| `agl` | same as `altitude` | (no terrain height available) |
| `cur_time` | system clock | `(float)time(nullptr)` |

**Auto-discovery**: Same pattern as the other Phase 5 plans — search the schema for `position_lla.{lat,lon,alt}` signals. If not found, FGBridge sends a parked position (e.g., KSC pad) and logs a warning. The FG window still shows the globe; it just doesn't move.

**Quaternion fallback**: If `euler_zyx` signals are absent but `attitude.{w,x,y,z}` are present, convert quaternion → Euler angles (ZYX convention) before sending.

---

## 5. Implementation

### FGBridge Class

```cpp
// include/daedalus/world/fg_bridge.hpp
#pragma once
#include "daedalus/data/signal_buffer.hpp"
#include "daedalus/data/signal_tree.hpp"

namespace daedalus {

class FGBridge {
public:
    struct Config {
        std::string host = "127.0.0.1";
        uint16_t    port = 5500;
        float       rate_hz = 30.0f;
    };

    explicit FGBridge(Config cfg = {});
    ~FGBridge();

    // Called once after schema is received
    void bind_signals(const SignalTree& tree);

    // Called each render frame — sends UDP if rate allows and signals bound
    void update(const SignalBufferMap& buffers, float dt);

    bool is_connected() const;    // socket open
    bool has_position() const;    // position signals found in schema

private:
    void send_packet(const SignalBufferMap& buffers);
    bool open_socket();

    Config cfg_;
    int    sock_fd_ = -1;

    // Signal indices resolved at bind_signals() time
    // -1 = not found in schema
    int idx_lat_   = -1, idx_lon_   = -1, idx_alt_   = -1;
    int idx_roll_  = -1, idx_pitch_ = -1, idx_yaw_   = -1;
    int idx_vn_    = -1, idx_ve_    = -1, idx_vd_    = -1;
    int idx_pdot_  = -1, idx_qdot_  = -1, idx_rdot_  = -1;

    float elapsed_ = 0.0f;
};

} // namespace daedalus
```

```cpp
// src/daedalus/world/fg_bridge.cpp
#include "daedalus/world/fg_bridge.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <ctime>

// ... (FGNetFDM struct + byte-swap helpers as above)

void FGBridge::bind_signals(const SignalTree& tree) {
    // Helper: search for signal by suffix pattern, return index in schema order
    auto find = [&](std::string_view suffix) -> int {
        for (int i = 0; i < (int)tree.signals().size(); ++i) {
            if (tree.signals()[i].name.ends_with(suffix))
                return i;
        }
        return -1;
    };

    idx_lat_   = find("position_lla.lat");
    idx_lon_   = find("position_lla.lon");
    idx_alt_   = find("position_lla.alt");
    idx_roll_  = find("euler_zyx.roll");
    idx_pitch_ = find("euler_zyx.pitch");
    idx_yaw_   = find("euler_zyx.yaw");
    idx_vn_    = find("velocity_ned.n");
    idx_ve_    = find("velocity_ned.e");
    idx_vd_    = find("velocity_ned.d");
    idx_pdot_  = find("omega_body.x");
    idx_qdot_  = find("omega_body.y");
    idx_rdot_  = find("omega_body.z");
}

void FGBridge::update(const SignalBufferMap& buffers, float dt) {
    if (sock_fd_ < 0 && !open_socket()) return;

    elapsed_ += dt;
    if (elapsed_ < 1.0f / cfg_.rate_hz) return;
    elapsed_ = 0.0f;

    send_packet(buffers);
}

void FGBridge::send_packet(const SignalBufferMap& buffers) {
    auto latest = [&](int idx, double fallback = 0.0) -> double {
        if (idx < 0) return fallback;
        auto it = buffers.find(idx);
        if (it == buffers.end() || it->second.empty()) return fallback;
        return it->second.latest();
    };

    constexpr double M_TO_FT = 3.28084;

    FGNetFDM pkt{};
    pkt.version   = htonl(24);
    pkt.longitude = htond(latest(idx_lon_));
    pkt.latitude  = htond(latest(idx_lat_));
    pkt.altitude  = htond(latest(idx_alt_));
    pkt.agl       = htonf((float)latest(idx_alt_));  // approx: no terrain height
    pkt.phi       = htonf((float)latest(idx_roll_));
    pkt.theta     = htonf((float)latest(idx_pitch_));
    pkt.psi       = htonf((float)latest(idx_yaw_));
    pkt.phidot    = htonf((float)latest(idx_pdot_));
    pkt.thetadot  = htonf((float)latest(idx_qdot_));
    pkt.psidot    = htonf((float)latest(idx_rdot_));

    double vd = latest(idx_vd_);
    pkt.climb_rate = htonf((float)(-vd * M_TO_FT));  // NED down → up, m/s → ft/s
    pkt.v_north    = htonf((float)(latest(idx_vn_) * M_TO_FT));
    pkt.v_east     = htonf((float)(latest(idx_ve_) * M_TO_FT));
    pkt.v_down     = htonf((float)(vd * M_TO_FT));

    pkt.visibility = htonf(10000.0f);
    pkt.cur_time   = htonf((float)time(nullptr));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(cfg_.port);
    inet_pton(AF_INET, cfg_.host.c_str(), &addr.sin_addr);

    sendto(sock_fd_, &pkt, sizeof(pkt), 0,
           (struct sockaddr*)&addr, sizeof(addr));
}
```

### Integration in App

```cpp
// In app.hpp — add alongside existing views:
FGBridge fg_bridge_;

// In App::on_schema_received() (where SignalTree is populated):
fg_bridge_.bind_signals(signal_tree_);

// In App::update() — alongside process_telemetry():
fg_bridge_.update(signal_buffers_, frame_dt_);

// Optional: status indicator in status bar:
// "FG: streaming" / "FG: no position signals" / "FG: socket error"
```

The FGBridge adds one member to App and two call sites. No new windows, no new rendering, no new threads.

### Status Bar Integration

Add a small indicator to the existing status bar:

```cpp
// In render_status_bar():
if (fg_bridge_.has_position()) {
    ImGui::TextColored({0.4f, 1.0f, 0.4f, 1.0f}, "FG OK");
} else {
    ImGui::TextDisabled("FG: no pos");
}
```

---

## 6. FlightGear Setup

### Installation

```bash
# NixOS / nix-shell:
nix-env -iA nixpkgs.flightgear

# Or add to devShell packages for convenience (no custom derivation needed):
# packages = [ ... pkgs.flightgear ... ];
```

### Launch Script

A shell script wraps the FlightGear invocation with the right flags:

```bash
#!/usr/bin/env bash
# scripts/launch_flightgear.sh
# Usage: ./launch_flightgear.sh [--lat=28.6] [--lon=-80.6] [--alt=0]

LAT="${1:-28.608}"   # Kennedy Space Center by default
LON="${2:--80.604}"
ALT="${3:-0}"

exec fgfs \
  --fdm=null \
  --native-fdm=socket,in,30,,5500,udp \
  --aircraft=SpaceShipOne \
  --lat="$LAT" \
  --lon="$LON" \
  --altitude="$ALT" \
  --timeofday=noon \
  --disable-ai-traffic \
  --disable-real-weather-fetch \
  --prop:/sim/rendering/quality-level=5 \
  "$@"
```

### Recommended Aircraft Models

FlightGear ships with or can easily download models appropriate for rocket visualization:

| Model | Notes |
|:------|:------|
| `SpaceShipOne` | Sub-orbital rocket plane, good silhouette |
| `SpaceShipTwo` | Similar, slightly larger |
| `X-15` | Rocket-powered research aircraft |
| `ufo` | Generic disc — useful for debugging position |

For a generic rocket shape, FlightGear's model database has community-contributed rocket addons. Alternatively, the 3D model `.ac` file can be replaced without changing anything else.

### Camera Modes in FlightGear

Once running, all standard FlightGear camera modes work out of the box:

| Key | View |
|:----|:-----|
| `v` | Cycle through views |
| `Ctrl+F` | Chase cam (follows vehicle) |
| `F10` | Tower view |
| Right-drag | Orbit around vehicle |
| Scroll | Zoom |

From orbital altitude, FlightGear renders the globe with atmosphere scattering. The vehicle is visible as a dot that can be zoomed in on.

---

## 7. File Structure

This approach adds very few files:

```
include/daedalus/world/
    fg_bridge.hpp           # FGNetFDM struct, byte-swap helpers, FGBridge class

src/daedalus/world/
    fg_bridge.cpp           # Implementation (~120 LOC)

scripts/
    launch_flightgear.sh    # Convenience launch script

tests/world/
    test_fg_bridge.cpp      # Byte-swap correctness, signal mapping, packet structure
```

**CMakeLists.txt changes**: Add `fg_bridge.cpp` to the library sources. No new dependencies — uses POSIX sockets (`sys/socket.h`), available everywhere.

---

## 8. Phased Rollout

### Step 1: UDP Bridge (1–2 days)

- Implement `FGBridge` class with FGNetFDM struct
- Add to App (bind_signals, update)
- Test with `--aircraft=ufo`: vehicle appears at correct lat/lon
- Verify attitude orientation looks correct

### Step 2: Signal Binding + Graceful Degradation (half day)

- Auto-discovery of position signals from schema
- Quaternion → Euler fallback if euler signals absent
- Status bar indicator
- Unit tests: byte-swap helpers, signal lookup

### Step 3: Launch Script + Documentation (half day)

- `scripts/launch_flightgear.sh` with sensible defaults
- README section: how to run FG alongside Daedalus
- Test with Icarus rocket sim end-to-end

### Step 4: Optional Enhancements

These are straightforward additions once Step 1–3 are working:

| Enhancement | Effort | Notes |
|:------------|:-------|:------|
| FG property HTTP control (time of day, weather) | 1 day | HTTP to localhost:5400 |
| Configurable UDP port/host via App settings | half day | For remote FG instances |
| Launch FG as a child process from Daedalus | 1 day | `fork()+exec()`, manage lifecycle |
| Custom rocket 3D model | external | Just a `.ac` or `.obj` file, FG handles loading |

---

## 9. Comparison vs Plans A and B

| Criterion | **Plan C: FlightGear** | **Plan A: Filament** | **Plan B: osgEarth** |
|:----------|:-----------------------|:---------------------|:---------------------|
| **New Daedalus code** | ~150 LOC | ~2,000–3,500 LOC | ~500–700 LOC |
| **Globe quality** | High (20yr rendering engine) | Low (DIY sphere) | High (full GIS stack) |
| **Vehicle on globe** | Yes | Yes | Yes |
| **Same window** | No (side-by-side) | Yes (FBO) | Yes (viewport) |
| **Trajectory trails** | No | Yes (custom VBO) | Yes (custom geometry) |
| **FOV cones** | No | Yes | Yes |
| **Camera control** | FG's (mouse in FG window) | Custom arcball | EarthManipulator |
| **Atmospheric effects** | Yes (built-in) | Minimal | Yes (SkySimple) |
| **High-altitude / space** | Yes (WGS84 global) | Yes | Yes |
| **WASM (Phase 6)** | No | Yes | No |
| **Nix packaging** | `pkgs.flightgear` (trivial) | Hard (custom derivation) | Hard (custom derivation) |
| **Time to working globe** | 1–2 days | 3–4 weeks | 2–3 weeks |

---

## 10. Risk Register

| Risk | Impact | Likelihood | Mitigation |
|:-----|:-------|:-----------|:-----------|
| **Byte order wrong** | Vehicle at wrong position / FG ignores packets | Medium | Verify with Wireshark or FG log output; `--log-level=debug` shows received FDM |
| **FGNetFDM version mismatch** | FG rejects packets silently | Low | Check installed FG version — v24 of the struct has been stable for many years |
| **Euler convention mismatch** | Vehicle attitude visually wrong | Medium | Icarus uses ZYX (aerospace standard), same as FG. Test with known attitude (e.g., 90° yaw). |
| **FG crashes at orbital altitude** | No 3D view above ~100km | Low | FG renders globe correctly at any altitude; models and terrain streaming handle high altitude fine |
| **UDP packet loss** | Vehicle position stutters | Very low | UDP on loopback is essentially lossless; FG interpolates between packets |
| **Model looks wrong** | Cosmetic only | Low | Choose appropriate model or replace the `.ac` file |
| **FlightGear startup time** | 15–30 sec before FG is ready | Low | Start FG before running Daedalus; FG accepts packets whenever it's ready |

---

## Appendix: Verifying the Bridge

To test without the full Daedalus app, a minimal UDP sender confirms the protocol works:

```bash
# Send a single test packet with Python (for sanity-checking):
python3 - <<'EOF'
import socket, struct, time, math

# FGNetFDM minimal packet (zeros except position + version)
# Big-endian: version(I) pad(I) lon(d) lat(d) alt(d) agl(f) phi(f) theta(f) psi(f)
# ... then 200+ bytes of zeros for the rest

version = 24
lat = math.radians(28.608)   # KSC
lon = math.radians(-80.604)
alt = 10.0                    # meters

header = struct.pack('>II', version, 0)
position = struct.pack('>dddffff', lon, lat, alt, alt, 0.0, 0.0, 0.0)
rest = b'\x00' * 400   # zeros for remaining fields

pkt = header + position + rest

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
for _ in range(100):
    sock.sendto(pkt, ('127.0.0.1', 5500))
    time.sleep(1/30)
EOF
```

If FlightGear places the "aircraft" over Kennedy Space Center, the bridge is working.

---

## Appendix: References

- [FlightGear FGNetFDM source](https://github.com/FlightGear/flightgear/blob/next/src/Network/net_fdm.hxx)
- [FlightGear external FDM documentation](https://wiki.flightgear.org/Howto:_Use_an_external_FDM)
- [FlightGear network protocol overview](https://wiki.flightgear.org/Property_Tree/Sockets)
- [FlightGear high altitude / space scenarios](https://wiki.flightgear.org/Atmospheric_flight)
- [X-15 addon for FlightGear](https://wiki.flightgear.org/X-15)
- [SpaceShipOne addon for FlightGear](https://wiki.flightgear.org/SpaceShipOne)
