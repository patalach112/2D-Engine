# Aegis2D (2D-Engine)

Lightweight, ultra-high-performance 2D game engine for **space flight simulation** games —
C++20, Vulkan 1.3, clean console-style architecture (low-level core + modular renderer,
batch rendering, offscreen PNG verification).

> **Status: Milestone 5 — Test Scene & Offscreen Renderer** ✅ (builds clean, unit tests green)
>
> All five milestones are implemented and compile-verified here. Runtime Vulkan execution
> (the windowed demos and the `aegis_rocket_dump` PNG capture) requires a machine with a
> Vulkan 1.3 driver — see "Verification" below.

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
| 5 | Test Scene & Offscreen Renderer (rocket scene → PNG dump) | ✅ done (this milestone) |

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
│   └── aegis_rocket_dump/ # Milestone 5 tool: deterministic rocket scene → rocket_frame.png (ctest)
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
  PNG round trip, …) is unit-tested — 46/46 tests pass (2,036 checks).
- `ctest` runs 3 cases: `aegis_tests`, `aegis_roundtrip` and `aegis_rocket_dump` —
  both driver-dependent tools **skip cleanly (exit 0)** without a driver.
- Driver-dependent paths cannot execute without a driver. On a Vulkan 1.3 machine:
  `ctest --preset=debug --output-on-failure` (runs the upload→readback byte-compare and
  renders + writes `rocket_frame.png`), or run the tools directly as shown above.

On your machine: `cmake --preset=debug && cmake --build --preset=debug && ctest --preset=debug --output-on-failure`.

## License

TBD — not yet chosen.
