// Aegis2D — value types shared by the UI kernel widgets (Milestone 9).
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

#include <array>
#include <string_view>

namespace aegis::ui {

/// Axis-aligned rectangle in pixels, top-left origin (x, y), size (w, h).
struct AEGIS_API UiRect {
    core::f32 x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
};

struct AEGIS_API UiColor {
    core::f32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

[[nodiscard]] constexpr UiColor rgb(const core::f32 r, const core::f32 g, const core::f32 b,
                                    const core::f32 a = 1.0f) noexcept {
    return UiColor{r, g, b, a};
}

enum class TextAlign : core::u8 { Left, Center, Right };

/// One immediate-mode draw command. The context keeps them in issue order,
/// which is the painter's draw order. Positions are pixels, top-left origin.
struct AEGIS_API UiDrawItem {
    enum Kind : core::u8 { Rect, Glyph } kind = Rect;
    UiRect rect{};                    ///< Rect: the rectangle. Glyph: the glyph quad (top-left origin).
    UiColor color{};
    std::array<core::f32, 4> uv{};    ///< Glyph only: atlas UV rect {u0, v0, u1, v1}
};

/// FNV-1a hash — stable widget ids across frames and builds.
[[nodiscard]] constexpr core::u64 hash_id(const std::string_view label) noexcept {
    core::u64 hash = 1469598103934665603ull;
    for (const char ch : label) {
        hash ^= static_cast<core::u64>(static_cast<unsigned char>(ch));
        hash *= 1099511628211ull;
    }
    return hash;
}

[[nodiscard]] constexpr core::u64 hash_id_indexed(const std::string_view label, const core::u64 index) noexcept {
    core::u64 hash = hash_id(label);
    hash ^= index + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
    return hash;
}

}  // namespace aegis::ui
