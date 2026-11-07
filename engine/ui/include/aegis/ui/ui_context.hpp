// Aegis2D — immediate-mode UI context (Milestone 9).
//
// The context is a pure CPU state machine: begin_frame() ingests one input
// snapshot, widgets hit-test and emit UiDrawItems in issue order (painter's
// algorithm), and the caller consumes items() once per frame. No Vulkan, no
// allocation growth after warm-up (vectors are reused), no global state.
//
// Interaction model: every interactive widget gets a stable UiId (hash of a
// label). The widget under the cursor becomes "hot"; pressing captures it as
// "active" until release. Widgets emitted later (painter's order = on top)
// win hit tests.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/ui/font_atlas.hpp>
#include <aegis/ui/ui_input.hpp>
#include <aegis/ui/ui_types.hpp>

#include <string_view>
#include <vector>

namespace aegis::ui {

struct UiButtonStyle {
    UiColor idle{0.16f, 0.19f, 0.26f, 0.92f};
    UiColor hover{0.23f, 0.28f, 0.38f, 0.96f};
    UiColor active{0.13f, 0.42f, 0.58f, 1.0f};
    UiColor text{0.92f, 0.94f, 0.98f, 1.0f};
    UiColor border{0.55f, 0.65f, 0.82f, 0.35f};
};

class UiContext {
public:
    /// `font` must outlive the context; pass a shared FontAtlas (or nullptr
    /// while its bake is pending — text() then draws nothing).
    UiContext() noexcept = default;
    explicit UiContext(const FontAtlas& font) noexcept { set_font(font); }

    /// Attaches the baked font atlas (safe to call once at startup).
    void set_font(const FontAtlas& font) noexcept { font_ = &font; }

    /// Resets per-frame state and ingests the input snapshot + viewport size.
    void begin_frame(const UiInput& input, core::f32 viewport_width_px, core::f32 viewport_height_px) noexcept;

    // --- primitives ---------------------------------------------------------

    /// Solid axis-aligned rectangle (pixels, top-left origin).
    void fill_rect(core::f32 x, core::f32 y, core::f32 w, core::f32 h, UiColor color) noexcept;
    /// 1..N-pixel outline as four thin rectangles (painter-consistent).
    void rect_outline(core::f32 x, core::f32 y, core::f32 w, core::f32 h, UiColor color,
                      core::f32 thickness_px = 1.0f) noexcept;
    /// Text; x is the anchor (left edge for Left, center for Center, right edge for Right).
    /// `size_px` is the em size. Pixel width is available via text_width_px().
    void text(core::f32 x, core::f32 y_top, core::f32 size_px, std::string_view string, UiColor color,
              TextAlign align = TextAlign::Left, FontWeight weight = FontWeight::Regular) noexcept;
    /// Width of `string` in pixels at `size_px` (per-weight advances, exact).
    [[nodiscard]] core::f32 text_width_px(std::string_view string, core::f32 size_px,
                                          FontWeight weight = FontWeight::Regular) const noexcept;
    /// Translucent underlay + panel fill + border — the standard panel chrome.
    void panel(core::f32 x, core::f32 y, core::f32 w, core::f32 h, UiColor fill,
               UiColor border) noexcept;

    // --- widgets ------------------------------------------------------------

    /// Push-button. Returns true on the frame the click completes (release inside).
    [[nodiscard]] bool button(core::u64 id, core::f32 x, core::f32 y, core::f32 w, core::f32 h,
                              std::string_view label, const UiButtonStyle& style = {}) noexcept;
    /// Toggle switch. Returns true on the frame it is flipped.
    [[nodiscard]] bool toggle(core::u64 id, core::f32 x, core::f32 y, core::f32 w, core::f32 h,
                              bool& state, std::string_view label) noexcept;
    /// Horizontal slider in [0, 1]; drag with the mouse. Returns the new value.
    [[nodiscard]] core::f64 slider_h(core::u64 id, core::f32 x, core::f32 y, core::f32 w, core::f32 h,
                                     core::f64 value) noexcept;
    /// Vertical slider in [0, 1] (bottom == 0); drag with the mouse.
    [[nodiscard]] core::f64 slider_v(core::u64 id, core::f32 x, core::f32 y, core::f32 w, core::f32 h,
                                     core::f64 value) noexcept;
    /// Non-interactive labelled bar (fuel / propellant readouts).
    void bar(core::f32 x, core::f32 y, core::f32 w, core::f32 h, core::f64 ratio, UiColor fill,
             UiColor track, UiColor border) noexcept;

    // --- ids -----------------------------------------------------------------

    [[nodiscard]] static core::u64 make_id(std::string_view label) noexcept { return hash_id(label); }
    [[nodiscard]] static core::u64 make_id(std::string_view label, core::u64 index) noexcept {
        return hash_id_indexed(label, index);
    }

    // --- output / state -------------------------------------------------------

    /// Everything emitted this frame, in painter's order. Valid until the next begin_frame().
    [[nodiscard]] const std::vector<UiDrawItem>& items() const noexcept { return items_; }
    [[nodiscard]] bool hovered(core::u64 id) const noexcept { return hot_ == id; }
    [[nodiscard]] bool captured(core::u64 id) const noexcept { return active_ == id; }
    [[nodiscard]] const UiInput& input() const noexcept { return input_; }
    [[nodiscard]] core::f32 viewport_width() const noexcept { return viewport_width_; }
    [[nodiscard]] core::f32 viewport_height() const noexcept { return viewport_height_; }
    [[nodiscard]] bool mouse_inside(core::f32 x, core::f32 y, core::f32 w, core::f32 h) const noexcept {
        return input_.mouse_x >= x && input_.mouse_x < x + w && input_.mouse_y >= y && input_.mouse_y < y + h;
    }

private:
    /// Shared press/capture/release dance for click-style widgets. Returns true on click.
    [[nodiscard]] bool click_widget(core::u64 id, bool hovered) noexcept;

    UiInput input_{};
    core::f32 viewport_width_ = 0.0f;
    core::f32 viewport_height_ = 0.0f;
    core::u64 hot_ = 0;    ///< id under the cursor (0 == none)
    core::u64 active_ = 0; ///< id captured by a press (0 == none)
    const FontAtlas* font_ = nullptr;  ///< baked atlas (non-owning); nullptr -> text() is a no-op
    std::vector<UiDrawItem> items_;
};

}  // namespace aegis::ui
