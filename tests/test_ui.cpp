// Aegis2D — unit tests for the aegis_ui kernel (Milestone 9).
#include <aegis/ui/ui_context.hpp>

#include "aegis_test.hpp"

#include <cmath>
#include <string>

using namespace aegis::ui;
namespace core = aegis::core;
using aegis::core::f32;

namespace {

UiInput mouse_at(const f32 x, const f32 y) {
    UiInput input;
    input.mouse_x = x;
    input.mouse_y = y;
    return input;
}

UiInput press_at(const f32 x, const f32 y) {
    UiInput input = mouse_at(x, y);
    input.mouse_down = true;
    input.mouse_pressed = true;
    return input;
}

UiInput hold_at(const f32 x, const f32 y) {
    UiInput input = mouse_at(x, y);
    input.mouse_down = true;
    return input;
}

UiInput release_at(const f32 x, const f32 y) {
    UiInput input = mouse_at(x, y);
    input.mouse_released = true;
    return input;
}

bool color_near(const UiColor a, const UiColor b, const f32 eps) {
    return std::fabs(a.r - b.r) <= eps && std::fabs(a.g - b.g) <= eps && std::fabs(a.b - b.b) <= eps &&
           std::fabs(a.a - b.a) <= eps;
}

}  // namespace

// ---------------------------------------------------------------- ids / keys

AEGIS_TEST(ui, "hash_id is deterministic and index-aware") {
    AEGIS_CHECK_EQ(hash_id("launch"), hash_id("launch"));
    AEGIS_CHECK(hash_id("launch") != hash_id("abort"));
    AEGIS_CHECK(hash_id_indexed("part", 1) != hash_id_indexed("part", 2));
    AEGIS_CHECK(hash_id_indexed("part", 1) != hash_id("part"));
}

AEGIS_TEST(ui, "key_bit assigns a unique bit per key and None is zero") {
    AEGIS_CHECK(key_bit(UiKey::None) == 0);
    core::u64 seen = 0;
    for (core::u32 i = 1; i < static_cast<core::u32>(UiKey::Count); ++i) {
        const core::u64 bit = key_bit(static_cast<UiKey>(i));
        AEGIS_CHECK(bit != 0 && (seen & bit) == 0);
        seen |= bit;
    }
}

AEGIS_TEST(ui, "UiInput down/pressed read the masks") {
    UiInput input;
    input.keys_down = key_bit(UiKey::Space) | key_bit(UiKey::M);
    input.keys_pressed = key_bit(UiKey::M);
    AEGIS_CHECK(input.down(UiKey::Space) && input.down(UiKey::M));
    AEGIS_CHECK(!input.down(UiKey::Escape));
    AEGIS_CHECK(input.pressed(UiKey::M) && !input.pressed(UiKey::Space));
}

// ------------------------------------------------------------------- font

AEGIS_TEST(ui, "glyph rows stay within five bits and match the atlas") {
    const std::array<core::u8, 7>& a = glyph_rows('A');
    AEGIS_CHECK_EQ(a[0], 0x0Eu);
    AEGIS_CHECK_EQ(a[3], 0x1Fu);
    const std::array<core::u8, 7>& space = glyph_rows(' ');
    for (const core::u8 row : space) {
        AEGIS_CHECK(row == 0);
    }
    for (const char ch : std::string("~0123456789!@#")) {
        for (const core::u8 row : glyph_rows(static_cast<core::u32>(ch))) {
            AEGIS_CHECK((row & 0xE0u) == 0);  // only the low five bits are used
        }
    }
    AEGIS_CHECK_EQ(glyph_rows(200)[0], glyph_rows('?')[0]);  // fallback
}

AEGIS_TEST(ui, "text width matches the glyph advance metric") {
    AEGIS_CHECK_EQ(text_width_px("", 16.0f), 0.0f);
    AEGIS_CHECK(std::fabs(text_width_px("A", 16.0f) - 16.0f * 0.625f) < 1e-6f);
    AEGIS_CHECK(std::fabs(text_width_px("AB", 16.0f) - (0.75f + 0.625f) * 16.0f) < 1e-6f);
    AEGIS_CHECK(std::fabs(text_width_px("ABCD", 8.0f) - (3.0f * 0.75f + 0.625f) * 8.0f) < 1e-5f);
    AEGIS_CHECK(text_advance_px(8.0f) == 6.0f);
}

// -------------------------------------------------------------- primitives

AEGIS_TEST(ui, "fill_rect emits one rect and zero-size rects are dropped") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.fill_rect(10.0f, 20.0f, 30.0f, 40.0f, rgb(0.5f, 0.6f, 0.7f, 1.0f));
    ctx.fill_rect(0.0f, 0.0f, 0.0f, 10.0f, rgb(1.0f, 1.0f, 1.0f, 1.0f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 1u);
    AEGIS_CHECK_EQ(items[0].kind, UiDrawItem::Rect);
    AEGIS_CHECK(items[0].rect.x == 10.0f && items[0].rect.y == 20.0f && items[0].rect.w == 30.0f &&
                items[0].rect.h == 40.0f);
}

AEGIS_TEST(ui, "rect_outline emits exactly four thin rects") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.rect_outline(0.0f, 0.0f, 100.0f, 50.0f, rgb(1.0f, 1.0f, 1.0f), 2.0f);
    AEGIS_CHECK_EQ(ctx.items().size(), 4u);
    AEGIS_CHECK(ctx.items()[0].rect.h == 2.0f);              // top
    AEGIS_CHECK(ctx.items()[1].rect.h == 2.0f);              // bottom
    AEGIS_CHECK(ctx.items()[2].rect.w == 2.0f);              // left
    AEGIS_CHECK(ctx.items()[3].rect.w == 2.0f);              // right
}

AEGIS_TEST(ui, "text emits one glyph per printable character with 6/8 advance") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.text(100.0f, 50.0f, 16.0f, "AB", rgb(1.0f, 1.0f, 1.0f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 2u);
    AEGIS_CHECK_EQ(items[0].kind, UiDrawItem::Glyph);
    AEGIS_CHECK(std::fabs(items[0].rect.x - 100.0f) < 1e-6f);
    AEGIS_CHECK(std::fabs(items[1].rect.x - (100.0f + 12.0f)) < 1e-6f);  // advance == 0.75 * 16
    AEGIS_CHECK_EQ(items[0].rows[0], 0x0Eu);                               // 'A' top row
    AEGIS_CHECK_EQ(items[1].rows[0], 0x0Fu);                               // 'B' top row
}

AEGIS_TEST(ui, "center and right aligned text shift the cursor") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.text(200.0f, 0.0f, 16.0f, "AB", rgb(1.0f, 1.0f, 1.0f), TextAlign::Center);
    const core::f32 expected_center = 200.0f - text_width_px("AB", 16.0f) * 0.5f;
    AEGIS_CHECK(std::fabs(ctx.items()[0].rect.x - expected_center) < 1e-4f);

    UiContext ctx2;
    ctx2.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx2.text(200.0f, 0.0f, 16.0f, "AB", rgb(1.0f, 1.0f, 1.0f), TextAlign::Right);
    const core::f32 expected_right = 200.0f - text_width_px("AB", 16.0f);
    AEGIS_CHECK(std::fabs(ctx2.items()[0].rect.x - expected_right) < 1e-4f);
}

AEGIS_TEST(ui, "non-printable bytes are skipped but still advance the pen") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    const char raw[] = {'A', static_cast<char>(0xC3), 'B', '\0'};
    ctx.text(0.0f, 0.0f, 16.0f, raw, rgb(1.0f, 1.0f, 1.0f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 2u);
    AEGIS_CHECK(std::fabs(items[1].rect.x - 2.0f * text_advance_px(16.0f)) < 1e-4f);
}

AEGIS_TEST(ui, "panel emits shadow + fill + outline") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(500.0f, 500.0f), 800.0f, 600.0f);
    ctx.panel(10.0f, 10.0f, 100.0f, 80.0f, rgb(0.1f, 0.1f, 0.12f, 0.9f), rgb(0.5f, 0.5f, 0.6f));
    AEGIS_CHECK_EQ(ctx.items().size(), 6u);  // shadow + fill + 4 outline edges
    AEGIS_CHECK(ctx.items()[0].rect.x == 13.0f);  // shadow offset
}

AEGIS_TEST(ui, "the item list resets each frame and preserves issue order") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 100.0f, 100.0f);
    ctx.fill_rect(0.0f, 0.0f, 1.0f, 1.0f, rgb(1.0f, 0.0f, 0.0f));
    ctx.text(0.0f, 5.0f, 8.0f, "A", rgb(0.0f, 1.0f, 0.0f));
    AEGIS_CHECK_EQ(ctx.items().size(), 2u);
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 100.0f, 100.0f);
    AEGIS_CHECK_EQ(ctx.items().size(), 0u);
}

// ----------------------------------------------------------------- widgets

AEGIS_TEST(ui, "button clicks on release over the button exactly once") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("launch");
    ctx.begin_frame(press_at(50.0f, 20.0f), 800.0f, 600.0f);
    AEGIS_CHECK(!ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH"));
    ctx.begin_frame(hold_at(50.0f, 20.0f), 800.0f, 600.0f);
    AEGIS_CHECK(!ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH"));
    ctx.begin_frame(release_at(50.0f, 20.0f), 800.0f, 600.0f);
    AEGIS_CHECK(ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH"));
    // The click fired on the release frame only.
    ctx.begin_frame(mouse_at(50.0f, 20.0f), 800.0f, 600.0f);
    AEGIS_CHECK(!ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH"));
}

AEGIS_TEST(ui, "button does not fire when the release lands outside") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("launch");
    ctx.begin_frame(press_at(50.0f, 20.0f), 800.0f, 600.0f);
    (void)ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH");
    ctx.begin_frame(release_at(500.0f, 400.0f), 800.0f, 600.0f);
    AEGIS_CHECK(!ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH"));
}

AEGIS_TEST(ui, "button click does not leak into a neighbour (capture is exclusive)") {
    UiContext ctx;
    const core::u64 a = UiContext::make_id("a");
    const core::u64 b = UiContext::make_id("b");
    ctx.begin_frame(press_at(20.0f, 20.0f), 800.0f, 600.0f);
    (void)ctx.button(a, 10.0f, 10.0f, 60.0f, 20.0f, "A");  // press A
    ctx.begin_frame(release_at(200.0f, 20.0f), 800.0f, 600.0f);
    // Release over B: neither button reports a click.
    AEGIS_CHECK(!ctx.button(a, 10.0f, 10.0f, 60.0f, 20.0f, "A"));
    AEGIS_CHECK(!ctx.button(b, 150.0f, 10.0f, 60.0f, 20.0f, "B"));
}

AEGIS_TEST(ui, "button becomes hover hot and reports capture") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("hover");
    ctx.begin_frame(mouse_at(20.0f, 20.0f), 800.0f, 600.0f);
    (void)ctx.button(id, 10.0f, 10.0f, 60.0f, 20.0f, "A");
    AEGIS_CHECK(ctx.hovered(id));
    AEGIS_CHECK(!ctx.captured(id));
    ctx.begin_frame(press_at(20.0f, 20.0f), 800.0f, 600.0f);
    (void)ctx.button(id, 10.0f, 10.0f, 60.0f, 20.0f, "A");
    AEGIS_CHECK(ctx.captured(id));
}

AEGIS_TEST(ui, "toggle flips its state on click only") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("sym");
    bool state = false;
    ctx.begin_frame(press_at(15.0f, 15.0f), 800.0f, 600.0f);
    (void)ctx.toggle(id, 10.0f, 10.0f, 40.0f, 18.0f, state, "SYM");
    ctx.begin_frame(release_at(15.0f, 15.0f), 800.0f, 600.0f);
    AEGIS_CHECK(ctx.toggle(id, 10.0f, 10.0f, 40.0f, 18.0f, state, "SYM"));
    AEGIS_CHECK(state);
    // A frame with no input does not flip again.
    ctx.begin_frame(mouse_at(15.0f, 15.0f), 800.0f, 600.0f);
    AEGIS_CHECK(!ctx.toggle(id, 10.0f, 10.0f, 40.0f, 18.0f, state, "SYM"));
    AEGIS_CHECK(state);
}

AEGIS_TEST(ui, "horizontal slider reports unchanged value without a drag") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("throttle");
    ctx.begin_frame(mouse_at(300.0f, 300.0f), 800.0f, 600.0f);  // far from the slider
    const core::f64 value = ctx.slider_h(id, 10.0f, 10.0f, 100.0f, 16.0f, 0.42);
    AEGIS_CHECK(value == 0.42);
}

AEGIS_TEST(ui, "horizontal slider follows the pointer during a drag") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("throttle");
    ctx.begin_frame(press_at(10.0f, 18.0f), 800.0f, 600.0f);
    core::f64 value = ctx.slider_h(id, 10.0f, 10.0f, 100.0f, 16.0f, 0.5);
    AEGIS_CHECK(std::fabs(value - 0.0) < 1e-6);  // jumps to the press position
    ctx.begin_frame(hold_at(60.0f, 18.0f), 800.0f, 600.0f);
    value = ctx.slider_h(id, 10.0f, 10.0f, 100.0f, 16.0f, value);
    AEGIS_CHECK(std::fabs(value - 0.5) < 1e-6);
    ctx.begin_frame(hold_at(110.0f, 18.0f), 800.0f, 600.0f);
    value = ctx.slider_h(id, 10.0f, 10.0f, 100.0f, 16.0f, value);
    AEGIS_CHECK(std::fabs(value - 1.0) < 1e-6);  // clamped to the track end
    ctx.begin_frame(release_at(110.0f, 18.0f), 800.0f, 600.0f);
    value = ctx.slider_h(id, 10.0f, 10.0f, 100.0f, 16.0f, value);
    AEGIS_CHECK(std::fabs(value - 1.0) < 1e-6);
}

AEGIS_TEST(ui, "vertical slider is bottom-zero and top-one") {
    UiContext ctx;
    const core::u64 id = UiContext::make_id("throttle_v");
    ctx.begin_frame(press_at(20.0f, 104.0f), 800.0f, 600.0f);  // near the bottom
    core::f64 value = ctx.slider_v(id, 10.0f, 10.0f, 20.0f, 100.0f, 0.5);
    AEGIS_CHECK(value < 0.1);
    ctx.begin_frame(hold_at(20.0f, 12.0f), 800.0f, 600.0f);  // near the top
    value = ctx.slider_v(id, 10.0f, 10.0f, 20.0f, 100.0f, value);
    AEGIS_CHECK(value > 0.9);
}

AEGIS_TEST(ui, "bar clamps its fill and emits no fill at zero ratio") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.bar(10.0f, 10.0f, 100.0f, 10.0f, 1.7, rgb(0.2f, 0.9f, 0.2f), rgb(0.1f, 0.1f, 0.1f),
            rgb(0.5f, 0.5f, 0.5f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 6u);  // track + fill + 4 outline edges
    AEGIS_CHECK(std::fabs(items[1].rect.w - 98.0f) < 1e-4f);  // fill spans (w - 2*1) * 1.0

    UiContext ctx2;
    ctx2.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx2.bar(10.0f, 10.0f, 100.0f, 10.0f, 0.0, rgb(1.0f, 1.0f, 1.0f), rgb(0.1f, 0.1f, 0.1f),
             rgb(0.5f, 0.5f, 0.5f));
    AEGIS_CHECK_EQ(ctx2.items().size(), 5u);  // track + 4 outline edges, no fill
}

AEGIS_TEST(ui, "ui is deterministic: identical input sequences produce identical items") {
    UiContext ctx_a, ctx_b;
    const core::u64 id = UiContext::make_id("deterministic");
    for (int frame = 0; frame < 4; ++frame) {
        const UiInput input = (frame == 0) ? press_at(30.0f, 25.0f) : hold_at(30.0f, 25.0f);
        ctx_a.begin_frame(input, 640.0f, 480.0f);
        ctx_b.begin_frame(input, 640.0f, 480.0f);
        (void)ctx_a.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "GO");
        (void)ctx_b.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "GO");
        ctx_a.text(5.0f, 50.0f, 12.0f, "FRAME", rgb(1.0f, 1.0f, 1.0f));
        ctx_b.text(5.0f, 50.0f, 12.0f, "FRAME", rgb(1.0f, 1.0f, 1.0f));
        AEGIS_CHECK_EQ(ctx_a.items().size(), ctx_b.items().size());
        for (std::size_t i = 0; i < ctx_a.items().size(); ++i) {
            AEGIS_CHECK(ctx_a.items()[i].rect.x == ctx_b.items()[i].rect.x);
            AEGIS_CHECK(ctx_a.items()[i].rect.y == ctx_b.items()[i].rect.y);
            AEGIS_CHECK(ctx_a.items()[i].rows == ctx_b.items()[i].rows);
        }
    }
}

AEGIS_TEST(ui, "viewport is exposed and color helpers are constexpr") {
    UiContext ctx;
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 123.0f, 456.0f);
    AEGIS_CHECK(ctx.viewport_width() == 123.0f && ctx.viewport_height() == 456.0f);
    constexpr UiColor c = rgb(0.1f, 0.2f, 0.3f, 0.9f);  // rgb() is constexpr
    AEGIS_CHECK(color_near(c, rgb(0.1f, 0.2f, 0.3f, 0.9f), 0.0f));
}
