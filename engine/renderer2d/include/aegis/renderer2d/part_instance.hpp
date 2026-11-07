#pragma once

#include <aegis/core/types.hpp>

namespace aegis::renderer2d {

/// Procedural part kinds (Milestone 9). Numeric values are baked into the
/// part fragment shader — keep in sync with shaders/part.frag.
enum class PartKind : core::u32 {
    FuelTank = 0,
    Pod = 1,
    Engine = 2,
    Plume = 3,
    Decoupler = 4,
    Separator = 5,
    Fairing = 6,
    Strut = 7,
};

/// Bit flags for a part instance.
inline constexpr core::f32 kPartFlagGhost = 1.0f;     ///< translucent placement ghost
inline constexpr core::f32 kPartFlagHighlight = 2.0f; ///< snap-target outline
inline constexpr core::f32 kPartFlagFlip = 4.0f;      ///< mirror the silhouette horizontally

/// One instance of the procedural part pipeline (Milestone 9).
///
/// Attribute mapping (binding 0, instance rate, stride 56):
///   location 0 = vec4 a_rect   (offset  0)  — xy = world center, zw = size
///   location 1 = vec4 a_tint   (offset 16)  — rgba base tint / livery
///   location 2 = float a_rot   (offset 32)  — CCW rotation, radians
///   location 3 = vec4 a_skin   (offset 36)  — x = PartKind, y = variant jitter [0,1),
///                                               z = clock (seconds), w = throttle [0,1]
///   location 4 = float a_flags (offset 52)  — kPartFlag* bitmask
///
/// Unlike SpriteInstance there is no per-instance softness: silhouettes are
/// analytic SDFs with fwidth() anti-aliasing, so edges stay crisp at every
/// zoom level without losing the whole instance to a radial fade term.
struct PartInstance {
    core::f32 x = 0.0f;
    core::f32 y = 0.0f;
    core::f32 width = 1.0f;
    core::f32 height = 1.0f;

    core::f32 r = 1.0f;
    core::f32 g = 1.0f;
    core::f32 b = 1.0f;
    core::f32 a = 1.0f;

    core::f32 rotation = 0.0f;
    core::f32 kind = 0.0f;
    core::f32 variant = 0.0f;
    core::f32 clock = 0.0f;
    core::f32 throttle = 0.0f;
    core::f32 flags = 0.0f;
};

static_assert(sizeof(PartInstance) == 56, "PartInstance must stay exactly 56 bytes");

}  // namespace aegis::renderer2d
