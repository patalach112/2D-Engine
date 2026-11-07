# Aegis2D (2D-Engine)

Lightweight, ultra-high-performance 2D game engine for **space flight simulation** games —
C++20, Vulkan 1.3, clean console-style architecture (low-level core + modular renderer,
batch rendering, offscreen PNG verification).

> **Status: Milestone 1 — Foundation & Vulkan Initialization** ✅ (builds clean, unit tests green)
>
> The repository currently builds and links a minimal but real Vulkan 1.3 engine core:
> windowed (GLFW → swapchain) or headless (offscreen) startup, dynamic rendering
> (no render passes), and a clear-loop demo. The GPU stack is fully exercised at build
> time; runtime execution requires a machine with a Vulkan 1.3 driver — see
> "Verification" below.

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
| 1 | Foundation & Vulkan Initialization (CMake, volk, glfw, instance, device, swapchain) | ✅ this milestone |
| 2 | VRAM Allocator & 2D Buffers (vertex/index allocators, TransferQueue) | pending |
| 3 | Universal 2D Batch Renderer (instanced rockets/particles/UI) | pending |
| 4 | Camera & Orbital Trajectory System (2D zoom camera, orbit/velocity lines) | pending |
| 5 | Test Scene & Offscreen Renderer (rocket scene → PNG dump) | pending |

## Repository layout

```
├── cmake/                 # AegisDeps.cmake (pinned FetchContent), warning flags
├── engine/
│   ├── core/              # aegis_core       — fixed-width types, Result<T>, logging, asserts (no Vulkan)
│   └── vk/                # aegis_vk         — volk instance, physical-device scoring, logical device,
│                          #                    swapchain (+recreate), GLFW surface
├── apps/
│   └── aegis_demo/        # Milestone 1 demo: window + clear loop (dynamic rendering)
├── tests/                 # zero-dependency unit test harness (aegis_test.hpp)
└── CMakePresets.json      # debug / release / ci / headless
```

## Design notes

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

## Verification

This repository's CI environment has **no Vulkan driver and no GPU**. What that means:

- Everything compiles warning-free on `debug`, `ci` (`-Werror`) and `headless` presets.
- All Vulkan-free logic (Result, logging, fixed-width types, vk_common helpers, …)
  is unit-tested and passes via `ctest`.
- Driver-dependent paths (instance/device creation, swapchain) cannot execute without
  a driver; `aegis_demo` exits early with a clear message in that case.

On your machine: `cmake --preset=debug && cmake --build --preset=debug && ctest --preset=debug --output-on-failure`.

## License

TBD — not yet chosen.
