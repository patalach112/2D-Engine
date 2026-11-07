#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

#include <algorithm>
#include <cstdint>

namespace aegis::renderer2d {

/// One instance of the vector-glyph pipeline (Milestone 10.5).
///
/// Same 40-byte stride and attribute locations as SpriteInstance, which lets
/// glyphs share the 40-byte instance buffer and ring-buffer machinery. The
/// atlas UVs travel as two 16-bit unorm channels packed into INTEGER words
/// (R32G32_UINT), same transport class as the old bitmap rows:
///
///   location 0 = vec4 a_rect   (offset  0)  — xy = world center, zw = quad size
///   location 1 = vec4 a_tint   (offset 16)  — rgba color
///   location 2 = uvec2 a_params (offset 32) — x = (u0, v0), y = (u1, v1), 16-bit unorm each
///
/// Coverage is sampled from the font atlas texture (bound as set 0, binding 0
/// by the glyph pipeline). 16-bit unorm UVs resolve an atlas up to 65536 px
/// with sub-texel precision — far beyond our 1024x512.
inline constexpr core::u32 kGlyphUvBits = 16;
inline constexpr core::u32 kGlyphUvMax = 0xFFFFu;

[[nodiscard]] constexpr core::u32 pack_unorm16(const core::f32 value) noexcept {
    const core::f32 clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<core::u32>(clamped * 65535.0f + 0.5f) & 0xFFFFu;
}

/// Pack a normalized UV pair into one u32 (x in the low half, y in the high).
[[nodiscard]] constexpr core::u32 pack_uv16(const core::f32 u, const core::f32 v) noexcept {
    return pack_unorm16(u) | (pack_unorm16(v) << kGlyphUvBits);
}

/// Inverse of pack_uv16 (used by tests).
constexpr void unpack_uv16(const core::u32 packed, core::f32& u, core::f32& v) noexcept {
    u = static_cast<core::f32>(packed & 0xFFFFu) / 65535.0f;
    v = static_cast<core::f32>(packed >> kGlyphUvBits) / 65535.0f;
}

struct AEGIS_API GlyphInstance {
    core::f32 x = 0.0f;
    core::f32 y = 0.0f;
    core::f32 width = 1.0f;
    core::f32 height = 1.0f;

    core::f32 r = 1.0f;
    core::f32 g = 1.0f;
    core::f32 b = 1.0f;
    core::f32 a = 1.0f;

    core::u32 uv_lo = 0;  ///< (u0, v0) packed unorm16 — top-left of the atlas cell
    core::u32 uv_hi = 0;  ///< (u1, v1) packed unorm16 — bottom-right of the atlas cell
};

static_assert(sizeof(GlyphInstance) == 40, "GlyphInstance must stay exactly 40 bytes");

/// Build a ready-to-draw glyph instance. `width`/`height` are the quad size in world units.
[[nodiscard]] inline GlyphInstance make_glyph(const core::f32 x, const core::f32 y, const core::f32 width,
                                              const core::f32 height, const core::f32 u0, const core::f32 v0,
                                              const core::f32 u1, const core::f32 v1, const core::f32 r,
                                              const core::f32 g, const core::f32 b, const core::f32 a) noexcept {
    GlyphInstance instance;
    instance.x = x;
    instance.y = y;
    instance.width = width;
    instance.height = height;
    instance.r = r;
    instance.g = g;
    instance.b = b;
    instance.a = a;
    instance.uv_lo = pack_uv16(u0, v0);
    instance.uv_hi = pack_uv16(u1, v1);
    return instance;
}

}  // namespace aegis::renderer2d
