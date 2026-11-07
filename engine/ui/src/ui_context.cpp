// Aegis2D — immediate-mode UI context implementation (Milestone 9).
#include <aegis/ui/ui_context.hpp>

#include <algorithm>
#include <cmath>

namespace aegis::ui {

using core::f32;



void UiContext::begin_frame(const UiInput& input, const core::f32 viewport_width_px,
                            const core::f32 viewport_height_px) noexcept {
    input_ = input;
    viewport_width_ = viewport_width_px;
    viewport_height_ = viewport_height_px;
    hot_ = 0;
    items_.clear();
    // active_ intentionally persists: the captured widget resolves the
    // capture itself when it sees !mouse_down (click = release over it).
}

void UiContext::fill_rect(const core::f32 x, const core::f32 y, const core::f32 w, const core::f32 h,
                          const UiColor color) noexcept {
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }
    UiDrawItem item;
    item.kind = UiDrawItem::Rect;
    item.rect = UiRect{x, y, w, h};
    item.color = color;
    items_.push_back(item);
}

void UiContext::rect_outline(const core::f32 x, const core::f32 y, const core::f32 w, const core::f32 h,
                             const UiColor color, const core::f32 thickness_px) noexcept {
    const core::f32 t = std::clamp(thickness_px, 1.0f, std::min(w, h) * 0.5f);
    fill_rect(x, y, w, t, color);
    fill_rect(x, y + h - t, w, t, color);
    fill_rect(x, y + t, t, h - 2.0f * t, color);
    fill_rect(x + w - t, y + t, t, h - 2.0f * t, color);
}

core::f32 UiContext::text_width_px(const std::string_view string, const core::f32 size_px,
                                   const FontWeight weight) const noexcept {
    if (font_ == nullptr || !font_->ready()) {
        return 0.0f;
    }
    return font_->measure(string, size_px, weight);
}

void UiContext::text(const core::f32 x, const core::f32 y_top, const core::f32 size_px,
                     const std::string_view string, const UiColor color, const TextAlign align,
                     const FontWeight weight) noexcept {
    if (font_ == nullptr || !font_->ready()) {
        return;
    }
    const FontAtlas& font = *font_;
    const core::f32 scale = size_px / font.baked_px();
    core::f32 cursor = x;
    if (align != TextAlign::Left) {
        const core::f32 width = font.measure(string, size_px, weight);
        cursor = (align == TextAlign::Center) ? x - width * 0.5f : x - width;
    }
    const core::f32 baseline = y_top + font.ascent(weight) * scale;
    for (std::size_t i = 0; i < string.size();) {
        const core::u32 codepoint = decode_utf8(string, i);
        const GlyphInfo& glyph = font.glyph(weight, codepoint);
        if (glyph.width > 0.0f && glyph.height > 0.0f) {
            UiDrawItem item;
            item.kind = UiDrawItem::Glyph;
            item.rect = UiRect{cursor + glyph.bearing_x * scale, baseline + glyph.bearing_y * scale,
                               glyph.width * scale, glyph.height * scale};
            item.color = color;
            item.uv = {glyph.u0, glyph.v0, glyph.u1, glyph.v1};
            items_.push_back(item);
        }
        cursor += glyph.advance * scale;
    }
}

void UiContext::panel(const core::f32 x, const core::f32 y, const core::f32 w, const core::f32 h,
                      const UiColor fill, const UiColor border) noexcept {
    // Drop shadow (offset, translucent dark) — painter's order puts it first.
    fill_rect(x + 3.0f, y + 4.0f, w, h, rgb(0.0f, 0.0f, 0.0f, 0.35f));
    fill_rect(x, y, w, h, fill);
    rect_outline(x, y, w, h, border, 1.0f);
}

bool UiContext::click_widget(const core::u64 id, const bool hovered) noexcept {
    if (hovered) {
        hot_ = id;
    }
    if (active_ == 0 && input_.mouse_pressed && hovered) {
        active_ = id;
    }
    if (active_ == id) {
        if (!input_.mouse_down) {
            active_ = 0;
            return hovered && input_.mouse_released;
        }
    }
    return false;
}

bool UiContext::button(const core::u64 id, const core::f32 x, const core::f32 y, const core::f32 w,
                       const core::f32 h, const std::string_view label, const UiButtonStyle& style) noexcept {
    const bool hovered = mouse_inside(x, y, w, h);
    const bool clicked = click_widget(id, hovered);
    const bool pressed = active_ == id && input_.mouse_down;
    const UiColor fill = pressed ? style.active : (hovered ? style.hover : style.idle);
    fill_rect(x, y, w, h, fill);
    rect_outline(x, y, w, h, style.border, 1.0f);
    text(x + w * 0.5f, y + (h - 14.0f) * 0.5f, 14.0f, label, style.text, TextAlign::Center);
    return clicked;
}

bool UiContext::toggle(const core::u64 id, const core::f32 x, const core::f32 y, const core::f32 w,
                       const core::f32 h, bool& state, const std::string_view label) noexcept {
    const bool hovered = mouse_inside(x, y, w, h);
    const bool clicked = click_widget(id, hovered);
    if (clicked) {
        state = !state;
    }
    const UiColor on_fill = rgb(0.20f, 0.62f, 0.38f, 0.95f);
    const UiColor off_fill = hovered ? rgb(0.24f, 0.28f, 0.37f, 0.95f) : rgb(0.17f, 0.20f, 0.28f, 0.95f);
    fill_rect(x, y, w, h, state ? on_fill : off_fill);
    rect_outline(x, y, w, h, rgb(0.55f, 0.65f, 0.82f, 0.35f), 1.0f);
    // Knob on the active side.
    const f32 pad = 2.0f;
    const f32 knob = h - 2.0f * pad;
    const f32 knob_x = state ? (x + w - pad - knob) : (x + pad);
    fill_rect(knob_x, y + pad, knob, knob, rgb(0.92f, 0.94f, 0.98f, 1.0f));
    if (!label.empty()) {
        text(x + w + 6.0f, y + (h - 14.0f) * 0.5f, 14.0f, label, rgb(0.85f, 0.88f, 0.93f, 1.0f));
    }
    return clicked;
}

core::f64 UiContext::slider_h(const core::u64 id, const core::f32 x, const core::f32 y, const core::f32 w,
                              const core::f32 h, const core::f64 value) noexcept {
    const bool hovered = mouse_inside(x, y, w, h);
    if (hovered) {
        hot_ = id;
    }
    if (active_ == 0 && input_.mouse_pressed && hovered) {
        active_ = id;
    }
    core::f64 result = std::clamp(value, 0.0, 1.0);
    if (active_ == id) {
        if (!input_.mouse_down) {
            active_ = 0;
        } else {
            result = std::clamp(static_cast<core::f64>(input_.mouse_x - x) / static_cast<core::f64>(w),
                               0.0, 1.0);
        }
    }
    const bool grabbed = active_ == id && input_.mouse_down;
    // Track + fill + handle.
    fill_rect(x, y + h * 0.5f - 2.0f, w, 4.0f, rgb(0.13f, 0.15f, 0.21f, 1.0f));
    fill_rect(x, y + h * 0.5f - 2.0f, static_cast<core::f32>(result) * w, 4.0f, rgb(0.35f, 0.75f, 0.95f, 1.0f));
    const f32 hx = x + static_cast<core::f32>(result) * w;
    const f32 handle = h * 0.72f;
    fill_rect(hx - handle * 0.5f, y + (h - handle) * 0.5f, handle, handle,
              grabbed ? rgb(0.55f, 0.90f, 1.0f, 1.0f) : rgb(0.80f, 0.86f, 0.94f, 1.0f));
    return result;
}

core::f64 UiContext::slider_v(const core::u64 id, const core::f32 x, const core::f32 y, const core::f32 w,
                              const core::f32 h, const core::f64 value) noexcept {
    const bool hovered = mouse_inside(x, y, w, h);
    if (hovered) {
        hot_ = id;
    }
    if (active_ == 0 && input_.mouse_pressed && hovered) {
        active_ = id;
    }
    core::f64 result = std::clamp(value, 0.0, 1.0);
    if (active_ == id) {
        if (!input_.mouse_down) {
            active_ = 0;
        } else {
            result = std::clamp(1.0 - static_cast<core::f64>(input_.mouse_y - y) / static_cast<core::f64>(h),
                               0.0, 1.0);
        }
    }
    const bool grabbed = active_ == id && input_.mouse_down;
    fill_rect(x + w * 0.5f - 3.0f, y, 6.0f, h, rgb(0.13f, 0.15f, 0.21f, 1.0f));
    const f32 fill_h = static_cast<core::f32>(result) * h;
    fill_rect(x + w * 0.5f - 3.0f, y + h - fill_h, 6.0f, fill_h, rgb(0.96f, 0.62f, 0.18f, 1.0f));
    const f32 hy = y + h - static_cast<core::f32>(result) * h;
    const f32 handle_h = 14.0f;
    fill_rect(x, hy - handle_h * 0.5f, w, handle_h,
              grabbed ? rgb(1.0f, 0.78f, 0.42f, 1.0f) : rgb(0.92f, 0.80f, 0.62f, 1.0f));
    return result;
}

void UiContext::bar(const core::f32 x, const core::f32 y, const core::f32 w, const core::f32 h,
                    const core::f64 ratio, const UiColor fill, const UiColor track, const UiColor border) noexcept {
    const f32 clamped = static_cast<f32>(std::clamp(ratio, 0.0, 1.0));
    fill_rect(x, y, w, h, track);
    if (clamped > 0.0f) {
        fill_rect(x + 1.0f, y + 1.0f, (w - 2.0f) * clamped, h - 2.0f, fill);
    }
    rect_outline(x, y, w, h, border, 1.0f);
}

}  // namespace aegis::ui
