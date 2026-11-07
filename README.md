# Aegis2D (2D-Engine)

Lightweight, ultra-high-performance 2D game engine for **space flight simulation** games —
C++20, Vulkan 1.3, clean console-style architecture (low-level core + modular renderer,
batch rendering, offscreen PNG verification).

> **Status: Milestone 8 — Physics Integration & Flight Dynamics** ✅ (builds clean, unit tests green)
>
> Phase-1 core exists (M1–M6 compile-verified; windowed Vulkan runs need a Vulkan 1.3 driver).
> Milestone 7 adds the SFS-style part system (grid assembly, staging, splits, mass/CoM/Izz).
> Milestone 8 adds deterministic rigid-body flight dynamics on top of it: propellant drain with
> flameouts, momentum-conserving stage separation, ballistic detached stages, quadratic drag,
> an exponential atmosphere, soft contact with impact detection — headless, f64 RK4 at 1/240 s,
> unit-tested and ctest-registered. The Vulkan runtime paths are driver-verified on your machine —
> see "Verification" below.

## Requirements

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.24 | Ninja generator recommended (`pip install cmake ninja`) |
| C++ compiler | C++20 | GCC 12+, Clang 15+, MSVC 2022 |
| gfortran | optional | Enables the Fortran orbit kernels (M6). MSYS2/MinGW64: `pacman -S mingw-w64-x86_64-gcc-fortran`; Debian: `apt install gfortran`. Auto-detected — see `AEGIS_ENABLE_FORTRAN` |
| Vulkan loader | 1.3 | Runtime only; `volk` dlopen's it (libvulkan.so.1 / vulkan-1.dll) |
| X11 or Wayland dev files | — | Linux windowed builds; auto-detected. Without them glfw falls back to its Null platform |

No system Vulkan SDK is needed **to build**: headers and [volk](https://github.com/zeux/volk)
are fetched via CMake `FetchContent` (pinned tags, see `cmake/AegisDeps.cmake`).

## Build

```bash
# Configure + build (auto-detects X11/Wayland; headless containers get glfw's Null platform)
cmake --preset=debug
cmake --build --preset=debug

# Windows/macOS or Linux with X11/Wayland dev files: same commands give you a real window.
# Fortran kernels (M6): AUTO (default — used when gfortran is found), ON (required), OFF (C++ fallback)
cmake --preset=debug -DAEGIS_ENABLE_FORTRAN=ON

# Force a backend explicitly if auto-detection is wrong for you:
cmake -S . -B build/manual -DGLFW_BUILD_WAYLAND=ON -DGLFW_BUILD_X11=OFF

# Run unit tests + driverless checks (aegis_roundtrip, aegis_rocket_dump)
ctest --preset=debug

# Run the demos (windowed)
./build/debug/apps/aegis_demo/aegis_demo             # M1: clear loop
./build/debug/apps/aegis_batch_demo/aegis_batch_demo # M3: batch renderer (rocket + HUD)
./build/debug/apps/aegis_orbit_demo/aegis_orbit_demo # M4: solar system (camera zoom, orbit path, velocity vector)

# Milestone 2 verification: arena → staging → transfer queue → readback → byte-compare
./build/debug/apps/aegis_roundtrip/aegis_roundtrip   # "ROUNDTRIP VERIFIED" on driver machines

# Milestone 5: render the deterministic rocket scene offscreen and write a PNG
./build/debug/tools/aegis_rocket_dump/aegis_rocket_dump                  # writes rocket_frame.png
./build/debug/tools/aegis_rocket_dump/aegis_rocket_dump shot.png         # custom output path

# Milestone 7: build the three-stage booster rocket, stage it end to end, verify everything
./build/debug/tools/aegis_assembly_check/aegis_assembly_check            # "ROCKET/ASSEMBLY_VERIFIED"

# Milestone 8: fly that rocket — liftoff, stage separations, 21.9 km apogee, propellant budget
./build/debug/tools/aegis_flight_check/aegis_flight_check                # "FLIGHT/ASCENT_VERIFIED"
```

`aegis_orbit_demo` controls: **scroll** = cursor-anchored zoom · **WASD/arrows** = pan ·
**F** = follow the planet · **R** = reset camera · **Esc** = quit.

### Headless / CI

```bash
cmake --preset=headless   # configures with AEGIS_WINDOWED=OFF: no windowing backend at all
cmake --preset=ci         # windowed backend + AEGIS_WERROR=ON (warnings-as-errors)
```

In a container without X11/Wayland dev files the windowed build still compiles end-to-end;
glfw uses its Null platform and the demos run as bounded smoke tests (120 null frames or
graceful exit at the point where a Vulkan driver is required) with exit code 0.
`aegis_rocket_dump` needs no window at all — offscreen rendering works on any headless
machine with a Vulkan 1.3 driver, and skips cleanly (exit 0) without one.

## Milestones

| # | Milestone | Status |
|---|-----------|--------|
| 1 | Foundation & Vulkan Initialization (CMake, volk, glfw, instance, device, swapchain) | ✅ done |
| 2 | VRAM Allocator & 2D Buffers (vertex/index allocators, TransferQueue) | ✅ done |
| 3 | Universal 2D Batch Renderer (instanced rockets/particles/UI) | ✅ done |
| 4 | Camera & Orbital Trajectory System (2D zoom camera, orbit/velocity lines) | ✅ done |
| 5 | Test Scene & Offscreen Renderer (rocket scene → PNG dump) | ✅ done |
| 6 | Fortran Math Bridge & Kepler Solver (Fortran kernels behind a C ABI; C++ fallback) | ✅ done |
| 7 | Part System & Vehicle Assembly (grid snap, staging, mass CoM) | ✅ done |
| 8 | Physics Integration & Flight Dynamics (RK4, propellant drain, separation Δv, ballistic stages, contact) | ✅ done (this milestone) |

## Repository layout

```
├── cmake/                 # AegisDeps.cmake (pinned FetchContent), warning flags
├── engine/
│   ├── core/              # aegis_core       — fixed-width types, Result<T>, logging, asserts (no Vulkan)
│   ├── parts/             # aegis_parts      — part templates registry, grid-based vehicle assembly,
│   │                      #                staging, decoupler splits, mass/CoM/Izz (Vulkan-free; M7)
│   ├── physics/           # aegis_physics    — rigid-body flight dynamics: fixed-step RK4, propellant
│   │                      #                drain + flameout, separation impulses (momentum-exact), ballistic
│   │                      #                detached stages, drag/atmosphere, contact, events (M8)
│   ├── orbit/             # aegis_orbit      — Keplerian orbital mechanics (two-body, Vulkan-free;
│                          #                Fortran kernels via iso_c_binding when gfortran is found,
│                          #                C++ fallback otherwise) + orbit_c.h C ABI + internal.hpp
│   ├── vk/                # aegis_vk         — volk instance, physical-device scoring, logical device
│   │                      #                (optional dedicated transfer queue), swapchain (+recreate),
│   │                      #                GLFW surface
│   ├── memory/            # aegis_memory     — FreeListAllocator (pure), device-local BufferArena,
│   │                      #                StagingRing (persistently mapped), GpuBuffer, TransferQueue
│   ├── renderer2d/        # aegis_renderer2d — BatchRenderer (instanced quads: sprite, particle, UI,
│   │                      #                line), Camera2D (2D zoom camera), make_orthographic
│   │                      #                (column-major, GLM-free), BatchStats
│   └── offscreen/         # aegis_offscreen  — surface-free offscreen renderer (RGBA8 → readback),
│                          #                PNG writer (miniz DEFLATE + CRC-32)
├── apps/
│   ├── aegis_demo/        # Milestone 1 demo: window + clear loop (dynamic rendering)
│   ├── aegis_roundtrip/   # Milestone 2 check: upload→readback→byte-compare (registered with ctest)
│   ├── aegis_batch_demo/  # Milestone 3 demo: rocket + starfield + HUD on the batch renderer
│   └── aegis_orbit_demo/  # Milestone 4 demo: sun/planet solar system; scroll-zoom camera, orbit path,
│                          #                velocity vector, pixel-anchored HUD
├── tools/
│   ├── aegis_rocket_dump/ # Milestone 5 tool: deterministic rocket scene → rocket_frame.png (ctest)
│   ├── aegis_assembly_check/ # Milestone 7 tool: three-stage rocket build/stage/verify (ctest)
│   └── aegis_flight_check/ # Milestone 8 tool: three-stage ascent, propellant budget, apogee (ctest)
├── tests/                 # zero-dependency unit test harness (aegis_test.hpp)
├── shaders/               # quad.vert|frag (sprites/particles/UI), line.vert|frag — GLSL → SPIR-V
└── CMakePresets.json      # debug / release / ci / headless
```

## Design notes (M1)

- **No exceptions** for engine control flow — fallible APIs return `Result<T>`.
- **No graphics API in `aegis_core`** — it is usable for tools/tests without Vulkan.
- **volk is statically compiled** (`aegis_volk`) and loaded once per process;
  the build never needs a Vulkan SDK, only a loader at runtime.
- **Vulkan 1.3 is mandatory**: instance creation rejects loaders older than 1.3, device
  creation requires `dynamicRendering` + `synchronization2`, and frames are recorded with
  dynamic rendering (`vkCmdBeginRendering`) instead of render passes.
- **Swapchain recreate hook** (`Swapchain::recreate`) chains `oldSwapchain` and preserves
  the current surface capabilities logic — used for window resizes now and for surface
  loss handling later.
- Validation layers are enabled automatically when `VK_LAYER_KHRONOS_validation` is
  installed (recommended on development machines; absent in minimal containers).

## Design notes (M2)

- **Best-fit free list** (`FreeListAllocator`) — pure C++, no Vulkan types, so splitting /
  coalescing / alignment / best-fit selection are unit-tested without a driver
  (see `tests/test_free_list.cpp`; randomized overlap checks over hundreds of interleaved
  alloc/free steps).
- **One device-local arena** (`BufferArena`) — a single large `DEVICE_LOCAL` VkBuffer with
  `VERTEX | INDEX | STORAGE | TRANSFER_SRC | TRANSFER_DST` usage, sub-allocated for vertex &
  index data. `BufferAllocation` handles are move-only RAII and return their segment to the
  free list on destruction. The arena asserts against outliving its allocations.
- **`staging ring`** (`StagingRing`) — one persistently mapped, host-visible (coherent when
  available) VkBuffer with bump allocation + wrap-around. `reset()` marks the whole ring
  reusable; callers must only call it once the GPU consumed prior uploads (blocking API makes
  that trivial right now).
- **TransferQueue** — prefers a dedicated TRANSFER-only queue family (created at device build
  time, CONCURRENT-sharded arena/ring buffers avoid ownership-transfer barriers); falls back
  silently to the graphics queue on GPUs without one (e.g. Intel iGPUs). All operations are
  blocking (fence wait) — load-time semantics; async submission comes with the frame system.
- **CONCURRENT sharing mode** between graphics and transfer families for the arena and the
  staging ring — removes all cross-family ownership barriers for load-time traffic.

## Design notes (M3)

- **No vertex buffers.** The six quad corners are synthesized in the vertex shader from
  `gl_VertexIndex`; every per-draw datum is a per-instance attribute (`a_rect` vec4 world
  x/y/w/h, `a_tint` vec4 rgba, `a_params` vec2 rotation-radians/softness), stride
  `sizeof(SpriteInstance)` == 40 bytes, `VK_VERTEX_INPUT_RATE_INSTANCE`. One instanced draw
  = 6 vertices, regardless of quad count.
- **One persistently mapped instance buffer per frame-in-flight slot**
  (`HOST_VISIBLE | HOST_COHERENT`, `max_instances` × 40 B, default 8192/frame); appending an
  instance is one `memcpy` into the mapped range — no allocation, no per-instance GPU traffic.
  If the buffer fills mid-frame the surplus is dropped with a one-shot warning.
- **Three pipelines from one shader pair** (`shaders/quad.vert|frag`): sprite (alpha blend),
  particle (`SRC_ALPHA` / `ONE` additive for glow), UI (alpha blend; separate handle so it can
  diverge later, e.g. with its own sampler/texture set). Softness in `a_params.y` fades the
  fragment radially (`length(v_local)`), so hard UI quads are solid and flame particles glow.
- **Painter's order with kind grouping**: consecutive same-kind draws share one batch
  (one `vkCmdDraw`); a sprite→particle→sprite sequence records three batches. Call
  `BatchRenderer::flush()` between kind groups to control cross-kind ordering explicitly.
- **Dynamic rendering only** (no render passes) with dynamic viewport/scissor; no depth
  attachment; culling disabled. Projection is a column-major mat4 (`make_orthographic`)
  pushed via a 64-byte vertex push constant — the renderer never sees a camera (that's M4).
- **Shaders compile at build time**: glslang is fetched pinned (15.0.0) via FetchContent and
  only its `glslang-standalone` binary is used (`--target-env vulkan1.3`); the engine links
  nothing from it. Missing/broken shader compilation fails the build, never the runtime.
- **Stats**: per-frame and lifetime `draw_calls` / `instance_count` with a per-kind
  breakdown (sprite/particle/ui), exposed via `BatchRenderer::stats()` and logged by the demo
  every 120 frames.

## Design notes (M4)

- **`Camera2D`** (`aegis_renderer2d`, Vulkan-free): world-to-screen mapping with y-up world
  vs y-down screen coordinates. `zoom` is measured in **pixels per world unit**;
  `world_per_pixel()` (its inverse) is used to keep screen-space element sizes constant
  across zoom levels (line thickness in the demo is authored in pixels and converted each
  frame). `zoom_at(cursor)` anchors the world point under the cursor during wheel zoom;
  `pan_pixels()` follows the grab-the-world convention; zoom is clamped to
  `[min_zoom, max_zoom]`. `view_projection()` centers an orthographic box on the camera and
  emits the same column-major mat4 convention as `make_orthographic()`.
- **`aegis::orbit`** (new Vulkan-free module): two-body Keplerian propagation in the world
  XY plane. `solve_kepler()` inverts Kepler's equation with a cubic initial guess +
  Newton–Raphson iterations (machine precision, eccentricities up to 0.9+). `state_at()`
  yields position, velocity, radius, true/mean/eccentric anomalies with
  `E`-param differentiation for velocity; specific orbital energy `v²/2 − μ/r` is conserved
  (unit-tested). `sample_path()` produces an even-`E` polyline for drawing orbits.
- **Specialized line pipeline** (`shaders/line.vert|frag`): one instance per world-space
  segment; the vertex shader expands the segment to a filled quad (direction → perpendicular
  normal, extend by half the thickness at both ends so polylines overlap at joints), the
  fragment shader draws a soft band (`smoothstep` on distance from the segment axis, clamped
  so `softness = 0` stays a hard edge). No `glLineWidth` (ignored on most drivers), no line
  topology — pure instances.
- **Bit-compatible instances**: `LineInstance` is exactly 40 bytes with attribute
  offsets/locations identical to `SpriteInstance` (static-asserted), so lines share the M3
  persistently mapped instance buffers and kind-grouped flush path. Lines are a fourth
  `PrimitiveKind` with its own pipeline; per-kind stats include `line_instances`.
- **`aegis_orbit_demo`**: sun + planet (a = 430 wu, e = 0.28, ω = 25°, T = 26 s) with the
  orbit polyline, a periapsis/apoapsis-marked velocity vector (scaled to periapsis speed —
  it visibly stretches near periapsis), a soft cyan orbit path, scroll-zoom/WASD/follow
  controls and a pixel-anchored HUD (border + panel + speed/radius bars).

## Design notes (M6)

- **Kernel split, one implementation of record per backend**: `engine/orbit/fortran/aegis_kernels.f90`
  hosts the numerical kernels (`solve_kepler`, `state_at`, `sample_path`) as free-form Fortran
  modules using `iso_c_binding`; the original Milestone-4 C++ code moved, numerically unchanged,
  into `engine/orbit/src/orbit_fallback.cpp` (`detail::fallback_*`). `orbit.cpp` keeps the public
  API and dispatches at compile time on `AEGIS_HAVE_FORTRAN` — no runtime dispatch, no virtual.
- **C ABI** (`aegis/orbit/orbit_c.h`, pure C): scalar-only arguments, `out_state[8]` /
  `out_xy[2*samples]` buffers, no structs by value, no allocations, no I/O — trivially callable
  from C++ and, later, from other toolchains. The header is always includable; the symbols exist
  only when the build found a Fortran compiler.
- **Numerical parity is tested, not hoped for**: with Fortran enabled, the public API *is* the
  Fortran path; `tests/test_orbit_kernels.cpp` A/B-compares it against the C++ fallback across a
  Kepler-anomaly grid (e ∈ {0, 0.28, 0.5, 0.9}, 997-point grid + out-of-range inputs), orbit states
  at 8 times over a full 26 s demo orbit (including t = 1e6 s), and the 128-point path sampling —
  tolerances 1e-7…1e-11 (position/velocity/anomalies). Without Fortran, the suite logs its disabled
  state and the fallback is covered by the untouched M4 test grid.
- **Build auto-detection**: `AEGIS_ENABLE_FORTRAN=AUTO|ON|OFF` (root `CMakePresets`-visible cache
  option, default AUTO). AUTO uses the Fortran kernels when `check_language(Fortran)` finds `gfortran`,
  otherwise configures the C++ fallback with a STATUS note; ON turns a missing compiler into a hard
  error (for CI). `AEGIS_FORTRAN_NATIVE=ON` opts into `-march=native`; default is `-O3
  -ffp-contract=off -fimplicit-none` for cross-machine IEEE parity (no fast-math, no contraction).
  `.mod` files stay in the build tree. Windows/MinGW users get Fortran from
  `pacman -S mingw-w64-x86_64-gcc-fortran`.
- **Why a fallback at all**: keeps the engine buildable and fully testable on machines without a
  Fortran toolchain (headless CI included) while the Fortran backend is the reference on real
  development machines — the A/B tests keep both honest.

## Design notes (M7)

- **Grid, not free placement**: every part template has an integer-cell footprint
  (`w`×`h`), rotations are 90° CCW steps plus a flip bit (SFS stock behaviour; the engine
  encodes both in one byte — low nibble steps 0..3, bit 4 flip applied before rotation), and
  placements are solved in f32 cell units with grid-alignment enforcement at snap time.
  `1 cell == 1 wu` (`aegis::parts::kCellWu`), keeping M8's physics in clean units.
- **POD parts, no per-instance overhead**: a placed part is a 16-byte POD (`template id`,
  grid position, rotation, fuel type, current propellant). Templates are static, shared
  `PartTemplate` definitions (13 built-ins, constexpr tables, original generic designations —
  no third-party game names or trade dress). Part indices are stable forever: splitting
  preserves original slots and leaves tombstones, so staging assignments and M8 flight
  references never dangle.
- **Node snapping**: attach points (`top`/`bottom`/`lateral`/`surface`) are declared per
  template in the unrotated frame; snapping solves the placement analytically (transformed
  node → child origin → grid quantise), requires opposed normals for structural pairs,
  rejects off-grid positions, and validates footprint occupancy against a cell→part hash
  map before committing. Nothing is placed that intersects an existing part.
- **Adjacency is derived, never stored**: `adjacency()` recomputes the edge set on demand
  (node-coincidence scan, O(nodes·log)) — no link bookkeeping to corrupt, nothing to keep
  consistent when parts come and go.
- **Removal = orphan cut**: `remove(part)` tombstones the target and every part left
  unattached to a command pod — SFS "delete and anything not still connected falls away".
- **Staging list**: ordered per-part actions (IgniteEngine / FireDecoupler /
  JettisonFairing) with `assign()`, an `auto_assign()` stack-depth heuristic (engines at
  depth 2·k, decouplers at 2·k+1; the bottom engine group is liftoff), deterministic firing
  order inside a stage (decouplers → fairings → ignitions, stable by part index), and
  sequential activation via `activate_next_stage()`. The next `n` stages fire in order;
  decouplers split the vehicle by cutting all incident adjacency edges — the pod-bearing
  component is adopted back into the assembly, detached pieces are returned to the caller.
  Decoupler carcasses stay attached (lower side), matching SFS conventions.
- **Mass properties without a DOM**: `mass_properties()` aggregates dry/propellant mass,
  CoM and a scalar 2D `Izz` (point-mass approximation) in O(n) — M8 gets inertia for free,
  M9 gets stack-rendering hints for free.
- **Symmetry is core API, not UI**: `mirror_snap()` re-places a part mirrored across a
  vertical/horizontal world axis (mirror rules `R(4−s)`, `R(2+4−s)` with the flip bit
  toggled), scanning the parent's nodes for the counterpart — x2/x4 booster symmetry is
  engine functionality ready for M9's builder UI.
- **`aegis_assembly_check`** (registered with ctest, always runs — no driver needed): builds
  a deterministic three-stage rocket with two radial boosters via the symmetry helper,
  stages it end to end, and asserts the whole pipeline (snap/mirror/split/CoM/staging/
  exhaustion) — 43 checks, prints `ROCKET/ASSEMBLY_VERIFIED`.
- **Name discipline**: templates reference only Aegis2D-original generic designations
  (Aegis MK-I Pod, Long-2 Tank, Sparrow/Kestrel/Longbow engines, MK-1 Decoupler, Radial
  Separator, Nose Fairing…) — no names from any existing game.
- **Deliberately out of scope for M7**: physics/dynamics (M8), propellant consumption (stored
  and aggregated only), rendering/UI + JSON data packs (M9), Δv estimation helpers.

## Design notes (M8)

- **`aegis_physics`** (`engine/physics/`, Vulkan-free, zero new dependencies): rigid-body flight
  dynamics for an aegis_parts vehicle. SI units inside world units (1 wu == 1 m), masses in the
  M7 mass units (kg), f64 integration. Flight state is planet-centred world coordinates; the
  assembly keeps its internal grid coordinates — positions of every material point are preserved
  exactly across splits (grid→world offsets rotate with `heading − π/2`).
- **Fixed-step RK4** at `VehicleFlight::kFixedDt = 1/240 s` (deterministic; `advance()` loops
  whole steps and carries the remainder; unit tests use `tick()`). Mass/CoM/Izz are constant
  inside a substep; propellant is drained once per tick **before** the RK4 step, so no substep
  ever sees half-drained tanks.
- **Propellant model**: ṁ = F / (Isp · g₀), g₀ = 9.80665. Each engine draws from the
  component-wide pool of tanks reachable from it (BFS over adjacency, sorted part-index order),
  proportional to remaining propellant — one shared pool per connected component (cross-feed),
  drained in proportion so co-burning engines share equally. A dry engine emits exactly one
  `EngineFlameout` and stops producing thrust.
- **Stage separation**: decouplers fire in M7 firing order; each fire is a two-body split
  between the detached piece and everything still attached — the **inverse-mass rule** with a
  running remaining mass, so linear momentum is conserved exactly at every step, including
  simultaneous multi-decoupler activations. A detached piece carries the vehicle velocity at
  the instant of its own fire plus its separation share; the line of centers
  (det CoM − adopted CoM) gives the separation axis — stack splits push along the axis,
  symmetric radial booster splits cancel in the adopted vehicle.
- **Detached stages are ballistic free-flyers**: gravity-only integration, surface clamp,
  tracked in `detached_stages()`. Fairings are deleted by M7 (jettison tombstones them) and are
  deliberately *not* tracked as debris.
- **Contact model**: airborne spawn only — a vehicle spawned at r ≤ R is pinned to the surface
  sphere (radial velocity removed, tangential preserved). Approach speed beyond
  `ContactConfig.impact_threshold` (default 5 m/s) → `Impacted` (energy-free dead stop, engines
  cut, `finished() == true`); otherwise a soft touchdown (`Grounded` + one-shot `Touchdown`).
  No pads or launch hardware.
- **Aerodynamics**: quadratic drag opposing velocity (constant `AeroConfig.reference_area`),
  exponential atmosphere (ρ₀ scale height H, hard zero above `atmosphere_top`), optional
  weathercock stability (opt-in `weathercock_gain`; 0 disables) and angular damping.
- **Events** (drain with `take_events()`): `EngineIgnited`, `EngineFlameout`, `StageSeparated`,
  `FairingJettisoned`, `Touchdown`, `Impact` — the hook surface for later milestones
  (audio/UI/HUD).
- **`aegis_flight_check`** (registered with ctest, always runs — no driver needed): flies the
  M7 demo rocket on an Earth-like body — liftoff on three Sparrows, liftoff-group separation at
  t = 15 s, Kestrel upper-stage burn, stage-2 separation, fairing jettison — asserting event
  counts, detached-stage tracking, the adopted-assembly state machine and the full propellant
  budget (1,680 kg across four tanks, component-wide proportional pool; 1,155 kg drained,
  apogee ≈ 21.9 km). Prints `FLIGHT/ASCENT_VERIFIED`.
- **Orbit oracle** (unit test): a coasting post-cutoff segment matches
  `state_at(elements_from_state(state))` within 1e-6 wu over 10 s — `elements_from_state`
  (new in `aegis_orbit`) recovers Keplerian elements (semi-major axis, eccentricity,
  periapsis angle) from a Cartesian state, and flags hyperbolic (unbound) trajectories.
- **Deliberately out of scope for M8**: rotating planet (no Coriolis), wind, multi-body /
  patched conics on the vehicle side, launch pads, auto-sized drag area, attitude control
  laws beyond the pitch/torque hook, fairings as tracked debris, fin aerodynamics, Δv
  estimation helpers.

## Design notes (M5)

- **`aegis_offscreen`**: a surface-free Vulkan path for headless frame capture. One private
  `VK_FORMAT_R8G8B8A8_UNORM` image (`COLOR_ATTACHMENT | TRANSFER_SRC`, `DEVICE_LOCAL`),
  recorded with the same dynamic-rendering + batch-renderer pipeline as the windowed demos;
  after `vkCmdEndRendering` an image barrier moves it to `TRANSFER_SRC_OPTIMAL` and
  `vkCmdCopyImageToBuffer` delivers tightly packed RGBA8 into a persistently mapped
  `HOST_VISIBLE` readback buffer. No window, no surface, no swapchain. The renderer borrows
  a device and a queue from the caller (headless `vkInstance`/`Device` with
  `enable_swapchain = false` work — see `tools/aegis_rocket_dump`).
- **PNG writer** (`aegis::offscreen::write_png`): 8-bit RGBA, no interlace, filter-type-0
  scanlines, zlib-format IDAT via **miniz** (pinned 3.0.2, FetchContent; nothing linked from
  zlib), CRC-32 per chunk. Verified by a driver-free unit test that parses the produced PNG
  (signature, IHDR dimensions, per-chunk CRCs), inflates the IDAT and compares the decoded
  pixels byte-for-byte — including a custom-stride case.
- **`aegis_rocket_dump`** renders a fixed, **deterministic** scenario (t = 7 s): a
  multi-part rocket (nose cone, body, stripe, fins, portholes, engine particles, nozzle
  glow) on a Keplerian orbit around the sun, composed by the zoom camera to frame both
  bodies; orbit path polyline, periapsis/apoapsis markers, velocity vector with arrowhead,
  and a pixel-anchored HUD (speed/radius bars) — writing `rocket_frame.png`.
  Registered as the `aegis_rocket_dump` ctest case; exits 0 with a clear SKIP message when
  no Vulkan driver exists.

## Verification

This repository's CI environment has **no Vulkan driver and no GPU**. What that means:

- Everything compiles warning-free on `debug`, `ci` (`-Werror`) and `headless` presets.
- All Vulkan-free logic (allocator, Result, logging, projection, camera, orbital mechanics,
  PNG round trip, parts/staging, flight dynamics & propellant drain, …) is unit-tested —
  129/129 tests pass (2,455 checks) with the C++ Fortran fallback path active (this container
  has no gfortran); with Fortran enabled the suite additionally gains the three A/B
  kernel-equivalence groups.
- `ctest` runs 5 cases: `aegis_tests`, `aegis_roundtrip`, `aegis_rocket_dump`,
  `aegis_assembly_check` and `aegis_flight_check` — the two driver-dependent tools **skip
  cleanly (exit 0)** without a driver; `aegis_assembly_check` and `aegis_flight_check` are
  driver-free and always run.
- Fortran kernel path: the container has no gfortran, so the .f90 itself is line-audited
  (132-col/literal-kind/continuation checks) rather than compiled. The ABI is machine-checked:
  `orbit_c.h` parses as C99 (-Wall -Wextra -Werror) and as C++; the `#ifdef AEGIS_HAVE_FORTRAN`
  dispatch path, out-buffer indexing and the A/B tests are all exercised by a C-ABI stub of the
  Fortran symbols (identical algorithm) — 16/16 tests pass. The real gfortran compile + link +
  A/B run happens on machines with gfortran installed.
- Driver-dependent paths cannot execute without a driver. On a Vulkan 1.3 machine:
  `ctest --preset=debug --output-on-failure` (runs the upload→readback byte-compare and
  renders + writes `rocket_frame.png`), or run the tools directly as shown above.

On your machine: `cmake --preset=debug && cmake --build --preset=debug && ctest --preset=debug --output-on-failure`.

## License

TBD — not yet chosen.
