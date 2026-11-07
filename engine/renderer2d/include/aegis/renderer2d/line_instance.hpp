#pragma once

#include <aegis/core/types.hpp>

#include <cstddef>

#include <aegis/renderer2d/sprite_instance.hpp>

namespace aegis::renderer2d {

/// One instance of the specialized line-segment pipeline (Milestone 4).
///
/// BIT-LAYOUT COMPATIBLE with SpriteInstance: identical 40-byte stride and
/// identical attribute locations/formats (vec4/vec4/vec2 @ offsets 0/16/32,
/// binding 0, instance rate). This lets all instance batches share one
/// persistently mapped buffer and one kind-grouped flush path while different
/// pipelines interpret the same bytes differently:
///
///   location 0 = vec4 a_rect   (offset  0)  — xy = segment start (world), zw = segment end (world)
///   location 1 = vec4 a_tint   (offset 16)  — rgba color
///   location 2 = vec2 a_params (offset 32)  — x = thickness in WORLD units
///                                             (multiply by Camera2D::world_per_pixel()
///                                              for constant screen-space width),
///                                             y = softness [0 = hard edge .. 1 = soft band]
struct LineInstance {
    core::f32 ax = 0.0f;
    core::f32 ay = 0.0f;
    core::f32 bx = 1.0f;
    core::f32 by = 0.0f;

    core::f32 r = 1.0f;
    core::f32 g = 1.0f;
    core::f32 b = 1.0f;
    core::f32 a = 1.0f;

    core::f32 thickness = 1.0f;
    core::f32 softness = 0.5f;
};

static_assert(sizeof(LineInstance) == 40,
              "LineInstance must stay exactly 40 bytes (same stride as SpriteInstance)");
static_assert(offsetof(LineInstance, ax) == offsetof(SpriteInstance, x));
static_assert(offsetof(LineInstance, ay) == offsetof(SpriteInstance, y));
static_assert(offsetof(LineInstance, bx) == offsetof(SpriteInstance, width));
static_assert(offsetof(LineInstance, by) == offsetof(SpriteInstance, height));
static_assert(offsetof(LineInstance, r) == offsetof(SpriteInstance, r));
static_assert(offsetof(LineInstance, thickness) == offsetof(SpriteInstance, rotation));
static_assert(offsetof(LineInstance, softness) == offsetof(SpriteInstance, softness));

}  // namespace aegis::renderer2d
