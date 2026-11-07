#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

namespace aegis::renderer2d {

/// One instance of the universal 2D quad pipeline.
///
/// Attribute mapping (binding 0, VK_VERTEX_INPUT_RATE_INSTANCE, stride 40):
///   location 0 = vec4 a_rect   (offset  0)  — xy = world position (center), zw = size
///   location 1 = vec4 a_tint   (offset 16)  — rgba tint, channels in [0, 1]
///   location 2 = vec2 a_params (offset 32)  — x = rotation (radians, CCW),
///                                             y = softness [0 = hard edge .. 1 = fully soft radial fade]
struct AEGIS_API SpriteInstance {
    core::f32 x = 0.0f;
    core::f32 y = 0.0f;
    core::f32 width = 1.0f;
    core::f32 height = 1.0f;

    core::f32 r = 1.0f;
    core::f32 g = 1.0f;
    core::f32 b = 1.0f;
    core::f32 a = 1.0f;

    core::f32 rotation = 0.0f;
    core::f32 softness = 0.0f;
};

static_assert(sizeof(SpriteInstance) == 40, "SpriteInstance must stay exactly 40 bytes (stride + offsets are baked into the vertex input state)");

}  // namespace aegis::renderer2d
