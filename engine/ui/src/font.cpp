// Aegis2D — embedded font implementation (Milestone 9).
#include <aegis/ui/font.hpp>

#include "font_data.inc"

namespace aegis::ui {

const std::array<core::u8, kGlyphRows>& glyph_rows(const core::u32 codepoint) noexcept {
    // Printable ASCII maps to table entries 0..94; everything else -> '?' (63).
    const core::u32 index = (codepoint >= 32 && codepoint <= 126) ? codepoint - 32 : 63 - 32;
    return kFont5x7[index];
}

core::f32 text_width_px(const std::string_view text, const core::f32 size_px) noexcept {
    if (text.empty()) {
        return 0.0f;
    }
    // Matches glyph.vert/frag exactly: 5px glyph + 1px gap in an 8px cell,
    // so advance = 6/8 of the cell height.
    const core::f32 advance = text_advance_px(size_px);
    const core::f32 glyph_width = size_px * (static_cast<core::f32>(kGlyphColumns) / static_cast<core::f32>(kGlyphCellCells));
    return (static_cast<core::f32>(text.size()) - 1.0f) * advance + glyph_width;
}

}  // namespace aegis::ui
