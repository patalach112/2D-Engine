#pragma once

#include <aegis/core/types.hpp>

#include <array>

namespace aegis::renderer2d {

/// Column-major 4x4 orthographic projection (GLM-free), ready to memcpy into a
/// GLSL mat4 push constant / uniform.
///
/// Maps the world-space box [left, right] × [bottom, top] × [near_z, far_z]
/// onto Vulkan clip space: x right, y up, z in [0, 1] by default.
///
/// The default depth range maps near_z → 0 and far_z → 1, matching Vulkan's
/// [0, 1] clip-space depth (no perspective divide adjustments needed on the
/// device side).
///
/// Usage (column-major elements, m[col * 4 + row]):
///   const auto projection = aegis::renderer2d::make_orthographic(-half_w, half_w, -half_h, half_h);
///   renderer.begin_frame(cmd, frame_index, projection.data(), extent);
[[nodiscard]] std::array<core::f32, 16> make_orthographic(core::f32 left,
                                                          core::f32 right,
                                                          core::f32 bottom,
                                                          core::f32 top,
                                                          core::f32 near_z = -1.0f,
                                                          core::f32 far_z = 1.0f) noexcept;

}  // namespace aegis::renderer2d
