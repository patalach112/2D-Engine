# Aegis2D (2D-Engine)

Lightweight, ultra-high-performance 2D game engine for **space flight simulation** games —
C++20, Vulkan 1.3, clean console-style architecture (low-level core + modular renderer,
batch rendering, offscreen PNG verification).

> **Status: Milestone 4 — Camera & Orbital Trajectory System** ✅ (builds clean, unit tests green)
>
> Milestones 1 (Vulkan foundation), 2 (device-local arena, staging ring, TransferQueue),
> 3 (instanced 2D batch renderer: sprites, particles, UI) and 4 (2D zoom camera, Keplerian
> orbital mechanics, specialized line-segment pipeline) are implemented and compile-verified
> here. Runtime Vulkan execution (the `aegis_orbit_demo` solar system scene included)
> requires a machine with a Vulkan 1.3 driver — see "Verification" below.

## Requirements

| Tool | Version | Notes |
|------|---------|-------|
| CMake | ≥ 3.24 | Ninja generator recommended (`pip install cmake ninja`) |
| C++ compiler | C++20 | GCC 12+, Clang 15+, MSVC 2022 |
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
# Force a backend explicitly if auto-detection is wrong for you:
cmake -S . -B build/manual -DGLFW_BUILD_WAYLAND=ON -DGLFW_BUILD_X11=OFF

# Run unit tests
ctest --preset=debug

# Run the demos (windowed)
./build/debug/apps/aegis_demo/aegis_demo             # M1: clear loop
./build/debug/apps/aegis_batch_demo/aegis_batch_demo # M3: batch renderer (rocket + HUD)
./build/debug/apps/aegis_orbit_demo/aegis_orbit_demo # M4: solar system (camera zoom, orbit path, velocity vector)

# Milestone 2 verification: arena → staging → transfer queue → readback → byte-compare
./build/debug/apps/aegis_roundtrip/aegis_roundtrip   # "ROUNDTRIP VERIFIED" on driver machines
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

## Milestones

| # | Milestone | Status |
|---|-----------|--------|
| 1 | Foundation & Vulkan Initialization (CMake, volk, glfw, instance, device, swapchain) | ✅ done |
| 2 | VRAM Allocator & 2D Buffers (vertex/index allocators, TransferQueue) | ✅ done |
| 3 | Universal 2D Batch Renderer (instanced rockets/particles/UI) | ✅ done |
| 4 | Camera & Orbital Trajectory System (2D zoom camera, orbit/velocity lines) | ✅ this milestone |
| 5 | Test Scene & Offscreen Renderer (rocket scene → PNG dump) | pending |

## Repository layout

```
├── cmake/                 # AegisDeps.cmake (pinned FetchContent), warning flags
├── engine/
│   ├── core/              # aegis_core       — fixed-width types, Result<T>, logging, asserts (no Vulkan)
│   ├── orbit/             # aegis_orbit      — Keplerian orbital mechanics (two-body, Vulkan-free)
│   ├── vk/                # aegis_vk         — volk instance, physical-device scoring, logical device
│   │                      #                (optional dedicated transfer queue), swapchain (+recreate),
│   │                      #                GLFW surface
│   ├── memory/            # aegis_memory     — FreeListAllocator (pure), device-local BufferArena,
│   │                      #                StagingRing (persistently mapped), GpuBuffer, TransferQueue
│   └── renderer2d/        # aegis_renderer2d — BatchRenderer (instanced quads: sprite, particle, UI,
│                          #                line), Camera2D (2D zoom camera), make_orthographic
│                          #                (column-major, GLM-free), BatchStats
├── apps/
│   ├── aegis_demo/        # Milestone 1 demo: window + clear loop (dynamic rendering)
│   ├── aegis_roundtrip/   # Milestone 2 check: upload→readback→byte-compare (registered with ctest)
│   ├── aegis_batch_demo/  # Milestone 3 demo: rocket + starfield + HUD on the batch renderer
│   └── aegis_orbit_demo/  # Milestone 4 demo: sun/planet solar system; scroll-zoom camera, orbit path,
│                          #                velocity vector, pixel-anchored HUD
├── tests/                 # zero-dependency unit test harness (aegis_test.hpp)
├── shaders/               # quad.vert|frag (sprites/particles/UI), line.vert|frag (Milestone 4) — GLSL → SPIR-V
├── tools/                 # reserved (Milestone 5 PNG dump)
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
- **Shaders compile at build time**: glslang is fetched pinned (14.3.0) via FetchContent and
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

## Verification

This repository's CI environment has **no Vulkan driver and no GPU**. What that means:

- Everything compiles warning-free on `debug`, `ci` (`-Werror`) and `headless` presets.
- All Vulkan-free logic (allocator, Result, logging, orthographic projection, camera,
  orbital mechanics, …) is unit-tested — 43/43 tests pass (1,963 checks), including
  conserved orbital energy, Kepler solver round-trips and cursor-anchored zoom stability.
- Driver-dependent paths cannot execute without a driver. `aegis_roundtrip` performs the
  full upload → arena → readback → byte-compare verification on any machine with a
  Vulkan 1.3 driver and prints `ROUNDTRIP VERIFIED` on success — it is registered as the
  `aegis_roundtrip` ctest case (skips cleanly, exit 0, when no driver exists).

On your machine: `cmake --preset=debug && cmake --build --preset=debug && ctest --preset=debug --output-on-failure`.

## License

TBD — not yet chosen.
