#pragma once

#include <aegis/core/types.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

namespace aegis::renderer2d {

/// One instance of the bitmap-glyph pipeline (Milestone 9).
///
/// Bit-layout compatible with SpriteInstance (same 40-byte stride, same
/// attribute locations/formats), which lets glyphs share the 40-byte instance
/// buffer and the ring-buffer machinery:
///
///   location 0 = vec4 a_rect   (offset  0)  — xy = world center, zw = cell size
///   location 1 = vec4 a_tint   (offset 16)  — rgba color
///   location 2 = vec2 a_params (offset 32)  — x = rotation bits (unused here),
///                                               y = bit-packed 5x7 rows
///
/// A 5x7 glyph is 35 bits. They are packed as two IEEE-float bit patterns
/// (exact for up to 24 significand bits): word0 (location 2 x) holds rows 0-2
/// (15 bits, LSB first), word1 (location 2 y) holds rows 3-6 (20 bits, LSB
/// first). The fragment shader recovers them with floatBitsToUint — no
/// texture, no memory.
inline constexpr core::u32 kGlyphRows = 7;     ///< pixel rows
inline constexpr core::u32 kGlyphColumns = 5;  ///< pixel columns
inline constexpr core::u32 kGlyphCell = 8;     ///< rendering cell (glyph + margin)

using GlyphRows = std::array<core::u8, kGlyphRows>;

/// Pack the 7 row bytes (low 5 bits each) into the two 32-bit words stored in
/// `rotation`/`softness`. Deterministic inverse of unpack_glyph_rows().
[[nodiscard]] constexpr std::pair<core::u32, core::u32> pack_glyph_rows(const GlyphRows& rows) noexcept {
    core::u32 lo = 0;  // rows 0-2 (15 bits)
    core::u32 hi = 0;  // rows 3-6 (20 bits)
    for (core::u32 row = 0; row < 3; ++row) {
        lo |= (static_cast<core::u32>(rows[row] & 0x1Fu) << (5u * row));
    }
    for (core::u32 row = 3; row < kGlyphRows; ++row) {
        hi |= (static_cast<core::u32>(rows[row] & 0x1Fu) << (5u * (row - 3u)));
    }
    return {lo, hi};
}

/// Inverse of pack_glyph_rows() (used by tests).
[[nodiscard]] constexpr GlyphRows unpack_glyph_rows(const core::u32 lo, const core::u32 hi) noexcept {
    GlyphRows rows{};
    for (core::u32 row = 0; row < 3; ++row) {
        rows[row] = static_cast<core::u8>((lo >> (5u * row)) & 0x1Fu);
    }
    for (core::u32 row = 3; row < kGlyphRows; ++row) {
        rows[row] = static_cast<core::u8>((hi >> (5u * (row - 3u))) & 0x1Fu);
    }
    return rows;
}

struct GlyphInstance {
    core::f32 x = 0.0f;
    core::f32 y = 0.0f;
    core::f32 width = 1.0f;
    core::f32 height = 1.0f;

    core::f32 r = 1.0f;
    core::f32 g = 1.0f;
    core::f32 b = 1.0f;
    core::f32 a = 1.0f;

    core::f32 rotation = 0.0f;  ///< bitcast of the packed row word 0 (rows 0-2)
    core::f32 softness = 0.0f;  ///< bitcast of the packed row word 1 (rows 3-6)
};

static_assert(sizeof(GlyphInstance) == 40, "GlyphInstance must stay exactly 40 bytes");

/// Build a ready-to-draw glyph instance. `size` is the cell height in world units.
[[nodiscard]] inline GlyphInstance make_glyph(const core::f32 x, const core::f32 y, const core::f32 size,
                                              const GlyphRows& rows, const core::f32 r, const core::f32 g,
                                              const core::f32 b, const core::f32 a) noexcept {
    const auto [lo, hi] = pack_glyph_rows(rows);
    GlyphInstance instance;
    instance.x = x;
    instance.y = y;
    instance.width = size;
    instance.height = size;
    instance.r = r;
    instance.g = g;
    instance.b = b;
    instance.a = a;
    core::u32 lo_bits = lo, hi_bits = hi;
    std::memcpy(&instance.rotation, &lo_bits, sizeof(core::u32));
    std::memcpy(&instance.softness, &hi_bits, sizeof(core::u32));
    return instance;
}

}  // namespace aegis::renderer2d
