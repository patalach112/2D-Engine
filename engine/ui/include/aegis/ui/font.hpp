// Aegis2D — embedded 5x7 bitmap font (Milestone 9).
//
// No font files, no textures: the 95 printable ASCII glyphs live in
// src/font_data.inc (committed, hand-transcribed). A glyph cell is 8x8 px at
// size == 8; the advance is 6 cells (5 + 1-column gap), so text_width_px()
// is exact and the glyph shader and this header can never disagree.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/ui/ui_types.hpp>

#include <array>
#include <string_view>

namespace aegis::ui {

inline constexpr core::u32 kGlyphColumns = 5;
inline constexpr core::u32 kGlyphRows = 7;
inline constexpr core::u32 kGlyphAdvanceCells = 6;  ///< 5px glyph + 1px gap
inline constexpr core::u32 kGlyphCellCells = 8;     ///< rendering cell (glyph + margin)

/// Row masks for `codepoint` (32..126); anything else maps to '?'.
[[nodiscard]] const std::array<core::u8, kGlyphRows>& glyph_rows(core::u32 codepoint) noexcept;

/// Width of `text` in pixels when the glyph cell height is `size_px`.
[[nodiscard]] core::f32 text_width_px(std::string_view text, core::f32 size_px) noexcept;

/// Height of a line of text in pixels.
[[nodiscard]] constexpr core::f32 text_height_px(const core::f32 size_px) noexcept { return size_px; }

/// Baseline-agnostic: returns the glyph cell height used for layout.
[[nodiscard]] constexpr core::f32 text_advance_px(const core::f32 size_px) noexcept {
    return size_px * (static_cast<core::f32>(kGlyphAdvanceCells) / static_cast<core::f32>(kGlyphCellCells));
}

}  // namespace aegis::ui
