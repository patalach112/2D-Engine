// Aegis2D — vector font atlas (Milestone 10.5).
//
// Replaces the hand-transcribed 5x7 bitmap font with real vector typography:
// two JetBrains Mono weights (OFL-1.1, subset to the printable ASCII range
// plus common technical symbols, embedded in src/font_jetbrains_*.inc) baked
// once at startup by stb_truetype into a single R8 coverage atlas. The atlas
// is uploaded to the GPU by the render layer (BatchRenderer::set_glyph_atlas)
// and sampled per glyph — coverage AA at any size, no pixelation.
//
// The atlas is a pure CPU object: no Vulkan, no allocation after construction,
// deterministic bytes (same input TTFs -> same atlas, every build).
#pragma once

#include <aegis/core/types.hpp>

#include <array>
#include <string_view>

namespace aegis::ui {

inline constexpr core::u32 kFontAtlasWidth = 1024;
inline constexpr core::u32 kFontAtlasHeight = 512;
/// The em size at which glyphs are rasterized into the atlas. Larger values
/// cost bake time and atlas area; text at display sizes (<= 28 px) stays
/// crisp with bilinear filtering at this size.
inline constexpr core::f32 kFontBakedPx = 45.0f;

/// Supported codepoints: printable ASCII (32..126) plus the extras in
/// kExtraCodepoints. Everything else maps to the fallback ('?').
inline constexpr std::array<core::u32, 24> kExtraCodepoints = {
    0x00B0,  // ° degree
    0x00B1,  // ± plus-minus
    0x00B2,  // ² superscript two
    0x00B3,  // ³ superscript three
    0x00B5,  // µ micro
    0x00B7,  // · middle dot
    0x00D7,  // × multiplication
    0x2013,  // – en dash
    0x2014,  // — em dash
    0x2018,  // ' left single quote
    0x2019,  // ' right single quote
    0x201C,  // " left double quote
    0x201D,  // " right double quote
    0x2022,  // • bullet
    0x2026,  // … ellipsis
    0x2190,  // ← left arrow
    0x2191,  // ↑ up arrow
    0x2192,  // → right arrow
    0x2193,  // ↓ down arrow
    0x2206,  // ∆ increment
    0x2212,  // − minus sign
    0x2248,  // ≈ almost equal
};
inline constexpr core::u32 kGlyphCount = 95 + static_cast<core::u32>(kExtraCodepoints.size());  // 119

enum class FontWeight : core::u8 { Regular = 0, SemiBold = 1 };

/// Decode one UTF-8 sequence at `i` (advanced past it). Malformed sequences
/// consume one byte and yield U+FFFD (the atlas maps unknown codepoints to '?').
[[nodiscard]] inline core::u32 decode_utf8(const std::string_view string, std::size_t& i) noexcept {
    const core::u8 b0 = static_cast<core::u8>(string[i]);
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    const auto cont = [&string](const std::size_t j, core::u8& out) noexcept {
        if (j >= string.size()) {
            return false;
        }
        const core::u8 c = static_cast<core::u8>(string[j]);
        if ((c & 0xC0) != 0x80) {
            return false;
        }
        out = static_cast<core::u8>(c & 0x3Fu);
        return true;
    };
    core::u8 c1 = 0, c2 = 0, c3 = 0;
    if ((b0 & 0xE0) == 0xC0) {
        if (cont(i + 1, c1)) {
            i += 2;
            return (static_cast<core::u32>(b0 & 0x1Fu) << 6) | c1;
        }
    } else if ((b0 & 0xF0) == 0xE0) {
        if (cont(i + 1, c1) && cont(i + 2, c2)) {
            i += 3;
            return (static_cast<core::u32>(b0 & 0x0Fu) << 12) | (static_cast<core::u32>(c1) << 6) | c2;
        }
    } else if ((b0 & 0xF8) == 0xF0) {
        if (cont(i + 1, c1) && cont(i + 2, c2) && cont(i + 3, c3)) {
            i += 4;
            return (static_cast<core::u32>(b0 & 0x07u) << 18) | (static_cast<core::u32>(c1) << 12) |
                   (static_cast<core::u32>(c2) << 6) | c3;
        }
    }
    ++i;
    return 0xFFFD;
}

/// Per-glyph metrics in *baked* pixels (divide by kFontBakedPx and multiply
/// by the requested size to scale). UVs are normalized atlas coordinates.
/// `bearing_y` is the stb convention: the bitmap TOP's offset from the
/// baseline in y-DOWN pixels (negative for tall glyphs) — add it to the
/// baseline row to get the quad's top edge in top-left-origin UI space.
struct GlyphInfo {
    core::f32 advance = 0.0f;    ///< pen advance
    core::f32 bearing_x = 0.0f;  ///< bitmap left edge relative to the pen
    core::f32 bearing_y = 0.0f;  ///< bitmap top edge relative to the baseline, y-DOWN (add to baseline row)
    core::f32 width = 0.0f;      ///< bitmap width
    core::f32 height = 0.0f;     ///< bitmap height
    core::f32 u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

class FontAtlas {
public:
    FontAtlas() noexcept;  // bakes both weights; `ready()` reports success

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] const core::u8* pixels() const noexcept { return pixels_.data(); }
    [[nodiscard]] core::u32 atlas_width() const noexcept { return kFontAtlasWidth; }
    [[nodiscard]] core::u32 atlas_height() const noexcept { return kFontAtlasHeight; }
    [[nodiscard]] constexpr core::f32 baked_px() const noexcept { return kFontBakedPx; }

    [[nodiscard]] const GlyphInfo& glyph(FontWeight weight, core::u32 codepoint) const noexcept {
        return glyphs_[weight == FontWeight::SemiBold ? 1u : 0u][glyph_index(codepoint)];
    }
    [[nodiscard]] core::f32 ascent(FontWeight weight) const noexcept { return ascent_[weight == FontWeight::SemiBold ? 1u : 0u]; }
    [[nodiscard]] core::f32 descent(FontWeight weight) const noexcept { return descent_[weight == FontWeight::SemiBold ? 1u : 0u]; }
    [[nodiscard]] core::f32 line_height(FontWeight weight) const noexcept { return line_height_[weight == FontWeight::SemiBold ? 1u : 0u]; }

    /// Index of `codepoint` in the glyph table; kFallbackSlot when unsupported.
    [[nodiscard]] static core::u32 glyph_index(core::u32 codepoint) noexcept;
    [[nodiscard]] static core::u32 fallback_index() noexcept { return 63 - 32; }  // '?'

    /// Width of `text` in pixels at `size_px` (per-weight metrics, UTF-8/ASCII
    /// byte iteration — the engine's UI strings are byte-oriented).
    [[nodiscard]] core::f32 measure(std::string_view text, core::f32 size_px,
                                    FontWeight weight = FontWeight::Regular) const noexcept;

private:
    std::array<core::u8, kFontAtlasWidth * kFontAtlasHeight> pixels_{};
    std::array<GlyphInfo, kGlyphCount> glyphs_[2]{};
    core::f32 ascent_[2]{};
    core::f32 descent_[2]{};
    core::f32 line_height_[2]{};
    bool ready_ = false;
};

}  // namespace aegis::ui
