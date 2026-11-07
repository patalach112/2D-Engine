// Aegis2D — vector font atlas implementation (Milestone 10.5).
#include <aegis/ui/font_atlas.hpp>

#include "font_jetbrains_regular.inc"
#include "font_jetbrains_semibold.inc"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
// Vendored third-party header — suppress its sign-conversion noise so the
// first-party -Werror gate (aegis_werror) stays meaningful.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wdouble-promotion"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "../third_party/stb/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstring>

namespace aegis::ui {

namespace {

constexpr core::u32 pack_range_slot(const core::u32 codepoint) noexcept {
    // ASCII printable maps to slots 0..94; extras follow in kExtraCodepoints order.
    if (codepoint >= 32 && codepoint <= 126) {
        return codepoint - 32;
    }
    for (core::u32 i = 0; i < static_cast<core::u32>(kExtraCodepoints.size()); ++i) {
        if (kExtraCodepoints[i] == codepoint) {
            return 95 + i;
        }
    }
    return kGlyphCount;  // not found marker
}

template <typename T, std::size_t N>
constexpr bool contains(const std::array<T, N>& arr, const T value) noexcept {
    for (const T entry : arr) {
        if (entry == value) {
            return true;
        }
    }
    return false;
}

}  // namespace

core::u32 FontAtlas::glyph_index(const core::u32 codepoint) noexcept {
    const core::u32 slot = pack_range_slot(codepoint);
    return slot < kGlyphCount ? slot : fallback_index();
}

FontAtlas::FontAtlas() noexcept {
    // Bake both weights into ONE atlas: Regular in the top half, SemiBold
    // below. stb PackBegin/PackEnd bracket the whole bake; PackFontRanges is
    // called once per weight.
    const std::array<std::pair<const core::u8*, core::u64>, 2> fonts{{
        {kFontJetBrainsMonoRegular.data(), kFontJetBrainsMonoRegular.size()},
        {kFontJetBrainsMonoSemiBold.data(), kFontJetBrainsMonoSemiBold.size()},
    }};

    std::array<int, kExtraCodepoints.size()> extras{};
    for (core::u32 i = 0; i < static_cast<core::u32>(kExtraCodepoints.size()); ++i) {
        extras[i] = static_cast<int>(kExtraCodepoints[i]);
    }

    stbtt_pack_context pack{};
    const int ok_begin = stbtt_PackBegin(&pack, pixels_.data(), static_cast<int>(kFontAtlasWidth),
                                         static_cast<int>(kFontAtlasHeight), 0, 1, nullptr);
    if (ok_begin == 0) {
        return;
    }

    for (core::u32 weight = 0; weight < fonts.size(); ++weight) {
        std::array<stbtt_packedchar, 95> ascii{};
        std::array<stbtt_packedchar, kExtraCodepoints.size()> ext{};

        stbtt_pack_range ranges[2]{};
        // STBTT_POINT_SIZE: kFontBakedPx maps ONE EM SQUARE, not the full
        // ascender->descender height — text() then scales it linearly as a CSS em.
        ranges[0].font_size = STBTT_POINT_SIZE(kFontBakedPx);
        ranges[0].num_chars = 95;
        ranges[0].first_unicode_codepoint_in_range = 32;
        ranges[0].chardata_for_range = ascii.data();
        ranges[1].font_size = STBTT_POINT_SIZE(kFontBakedPx);
        ranges[1].num_chars = static_cast<int>(kExtraCodepoints.size());
        ranges[1].array_of_unicode_codepoints = extras.data();
        ranges[1].chardata_for_range = ext.data();

        const int ok_ranges = stbtt_PackFontRanges(&pack, fonts[weight].first, 0, ranges, 2);
        if (ok_ranges == 0) {
            stbtt_PackEnd(&pack);
            return;
        }

        constexpr core::f32 inv_w = 1.0f / static_cast<core::f32>(kFontAtlasWidth);
        constexpr core::f32 inv_h = 1.0f / static_cast<core::f32>(kFontAtlasHeight);
        auto fill = [&](const stbtt_packedchar* const pc, const core::u32 slot_offset, const core::u32 count) {
            for (core::u32 i = 0; i < count; ++i) {
                const stbtt_packedchar& p = pc[i];
                GlyphInfo info;
                info.advance = p.xadvance;
                info.bearing_x = p.xoff;
                info.bearing_y = p.yoff;  // y-DOWN offset from the baseline to the bitmap top
                info.width = static_cast<core::f32>(p.x1 - p.x0);
                info.height = static_cast<core::f32>(p.y1 - p.y0);
                info.u0 = static_cast<core::f32>(p.x0) * inv_w;
                info.v0 = static_cast<core::f32>(p.y0) * inv_h;
                info.u1 = static_cast<core::f32>(p.x1) * inv_w;
                info.v1 = static_cast<core::f32>(p.y1) * inv_h;
                glyphs_[weight][slot_offset + i] = info;
            }
        };
        fill(ascii.data(), 0, 95);
        fill(ext.data(), 95, static_cast<core::u32>(kExtraCodepoints.size()));

        // Vertical metrics (scaled to baked pixels) for baseline placement.
        stbtt_fontinfo info{};
        if (stbtt_InitFont(&info, fonts[weight].first, stbtt_GetFontOffsetForIndex(fonts[weight].first, 0))) {
            int ascent = 0, descent = 0, line_gap = 0;
            stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
            const core::f32 scale = stbtt_ScaleForMappingEmToPixels(&info, kFontBakedPx);
            ascent_[weight] = static_cast<core::f32>(ascent) * scale;
            descent_[weight] = static_cast<core::f32>(-descent) * scale;  // positive below baseline
            line_height_[weight] = static_cast<core::f32>(ascent - descent + line_gap) * scale;
        } else {
            ascent_[weight] = kFontBakedPx * 0.75f;
            descent_[weight] = kFontBakedPx * 0.25f;
            line_height_[weight] = kFontBakedPx;
        }
    }

    stbtt_PackEnd(&pack);
    ready_ = true;
}

core::f32 FontAtlas::measure(const std::string_view text, const core::f32 size_px,
                             const FontWeight weight) const noexcept {
    const core::f32 s = size_px / kFontBakedPx;
    core::f32 width = 0.0f;
    for (std::size_t i = 0; i < text.size();) {
        const core::u32 codepoint = decode_utf8(text, i);
        width += glyph(weight, codepoint).advance * s;
    }
    return width;
}

}  // namespace aegis::ui
