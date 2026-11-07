# Aegis2D (2D-Engine)

Lightweight, ultra-high-performance 2D game engine for **space flight simulation** games —
C++20, Vulkan 1.3, clean console-style architecture (low-level core + modular renderer,
batch rendering, offscreen PNG verification).

> **Status: Milestone 2 — VRAM Allocator & 2D Buffers** ✅ (builds clean, unit tests green)
>
> Device-local GPU arena with a best-fit free list, a persistently mapped staging ring
> and a TransferQueue (dedicated TRANSFER-family when available, graphics-queue fallback)
> are in place and byte-verified end-to-end on driver machines via `aegis_roundtrip`.
> See "Verification" below for what this means on machines without a GPU.

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

# Run the demo (opens a window and clears it; close with the window button)
./build/debug/apps/aegis_demo/aegis_demo

# Milestone 2 verification: arena → staging → transfer queue → readback → byte-compare
./build/debug/apps/aegis_roundtrip/aegis_roundtrip   # "ROUNDTRIP VERIFIED" on driver machines
```

### Headless / CI

```bash
cmake --preset=headless   # configures with AEGIS_WINDOWED=OFF: no windowing backend at all
cmake --preset=ci         # windowed backend + AEGIS_WERROR=ON (warnings-as-errors)
```

In a container without X11/Wayland dev files the windowed build still compiles end-to-end;
glfw uses its Null platform and `aegis_demo` runs as a bounded smoke test (120 null frames
or graceful early exit when a Vulkan driver is absent) with exit code 0.

## Milestones

| # | Milestone | Status |
|---|-----------|--------|
| 1 | Foundation & Vulkan Initialization (CMake, volk, glfw, instance, device, swapchain) | ✅ done |
| 2 | VRAM Allocator & 2D Buffers (vertex/index allocators, TransferQueue) | ✅ this milestone |
| 3 | Universal 2D Batch Renderer (instanced rockets/particles/UI) | pending |
| 4 | Camera & Orbital Trajectory System (2D zoom camera, orbit/velocity lines) | pending |
| 5 | Test Scene & Offscreen Renderer (rocket scene → PNG dump) | pending |

## Repository layout

```
├── cmake/                 # AegisDeps.cmake (pinned FetchContent), warning flags
├── engine/
│   ├── core/              # aegis_core       — fixed-width types, Result<T>, logging, asserts (no Vulkan)
│   ├── vk/                # aegis_vk         — volk instance, physical-device scoring, logical device
│   │                      #                    (optional dedicated transfer queue), swapchain (+recreate),
│   │                      #                    GLFW surface
│   └── memory/            # aegis_memory     — FreeListAllocator (pure), device-local BufferArena,
│                          #                    StagingRing (persistently mapped), GpuBuffer, TransferQueue
├── apps/
│   ├── aegis_demo/        # Milestone 1 demo: window + clear loop (dynamic rendering)
│   └── aegis_roundtrip/   # Milestone 2 check: upload→readback→byte-compare (registered with ctest)
├── tests/                 # zero-dependency unit test harness (aegis_test.hpp)
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

## Verification

This repository's CI environment has **no Vulkan driver and no GPU**. What that means:

- Everything compiles warning-free on `debug`, `ci` (`-Werror`) and `headless` presets.
- All Vulkan-free logic (free-list allocator, Result, logging, fixed-width types, …)
  is unit-tested and passes via `ctest`.
- Driver-dependent paths cannot execute without a driver. `aegis_roundtrip` performs the
  full upload → arena → readback → byte-compare verification on any machine with a
  Vulkan 1.3 driver and prints `ROUNDTRIP VERIFIED` on success — it is registered as the
  `aegis_roundtrip` ctest case (skips cleanly, exit 0, when no driver exists).

On your machine: `cmake --preset=debug && cmake --build --preset=debug && ctest --preset=debug --output-on-failure`.

## License

TBD — not yet chosen.
