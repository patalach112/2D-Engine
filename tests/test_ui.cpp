// Aegis2D — unit tests for the aegis_ui kernel (Milestone 9).
#include <aegis/ui/ui_context.hpp>

#include "aegis_test.hpp"

#include <cmath>
#include <cstring>
#include <string>

// One shared baked atlas for the whole binary (bakes once, deterministic).
const aegis::ui::FontAtlas& test_font() {
    static const aegis::ui::FontAtlas font;
    return font;
}

// rect.x carries each glyph's left-side bearing; the pen advances by the
// mono advance. These helpers turn that back into exact expectations.
aegis::core::f32 bearing_px(const aegis::ui::FontAtlas& font, const char ch, const aegis::core::f32 size_px) {
    return font.glyph(aegis::ui::FontWeight::Regular, static_cast<aegis::core::u32>(ch)).bearing_x *
           (size_px / aegis::ui::kFontBakedPx);
}

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

AEGIS_TEST(ui, "the baked vector atlas is ready and metrics are sane") {
    const FontAtlas& font = test_font();
    AEGIS_CHECK(font.ready());
    AEGIS_CHECK(font.ascent(FontWeight::Regular) > 0.0f);
    AEGIS_CHECK(font.descent(FontWeight::Regular) > 0.0f);
    AEGIS_CHECK(font.line_height(FontWeight::Regular) >= font.ascent(FontWeight::Regular));
    // JetBrains Mono is strictly monospaced: every printable glyph advances
    // 0.6em; the space glyph occupies no atlas cell.
    const core::f32 advance = font.glyph(FontWeight::Regular, 'A').advance;
    AEGIS_CHECK(std::fabs(advance - 0.6f * aegis::ui::kFontBakedPx) < 1.0f);
    for (const char ch : std::string("~0123456789!@#AZ ")) {
        const GlyphInfo& g = font.glyph(FontWeight::Regular, static_cast<core::u32>(ch));
        AEGIS_CHECK(std::fabs(g.advance - advance) < 1e-3f);
        AEGIS_CHECK(g.u0 >= 0.0f && g.v0 >= 0.0f && g.u1 <= 1.0f && g.v1 <= 1.0f);
        AEGIS_CHECK((g.u1 > g.u0 && g.v1 > g.v0) || ch == ' ');  // space has no bitmap
    }
    // Unknown codepoints fall back to '?'; the extras (∆ − · …) are reachable.
    AEGIS_CHECK_EQ(FontAtlas::glyph_index(200), FontAtlas::fallback_index());   // é not in the subset
    AEGIS_CHECK(FontAtlas::glyph_index(0x2206) != FontAtlas::fallback_index());  // ∆ present
    AEGIS_CHECK(FontAtlas::glyph_index(0x2212) != FontAtlas::fallback_index());  // − present
}

AEGIS_TEST(ui, "the atlas bakes deterministically") {
    const FontAtlas fresh;
    const FontAtlas& cached = test_font();
    AEGIS_CHECK(fresh.ready());
    AEGIS_CHECK(std::memcmp(fresh.pixels(), cached.pixels(),
                            static_cast<std::size_t>(fresh.atlas_width()) * fresh.atlas_height()) == 0);
}

AEGIS_TEST(ui, "text width matches the glyph advance metric") {
    const FontAtlas& font = test_font();
    AEGIS_CHECK_EQ(font.measure("", 16.0f), 0.0f);
    const core::f32 advance_16 = font.measure("A", 16.0f);
    AEGIS_CHECK(std::fabs(font.measure("AB", 16.0f) - 2.0f * advance_16) < 1e-4f);
    AEGIS_CHECK(std::fabs(font.measure("ABCD", 8.0f) - 4.0f * advance_16 * 0.5f) < 1e-3f);
    // SemiBold advances match Regular (same hmtx in the subset).
    AEGIS_CHECK(std::fabs(font.measure("ABCD", 16.0f, FontWeight::SemiBold) -
                          font.measure("ABCD", 16.0f)) < 1e-4f);
}

AEGIS_TEST(ui, "utf-8 extras decode to single glyphs and advance the pen") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.text(0.0f, 0.0f, 16.0f, "A\u2206\u2212B\u2014", rgb(1.0f, 1.0f, 1.0f));  // A ∆ − B —
    AEGIS_CHECK_EQ(ctx.items().size(), 5u);
    AEGIS_CHECK_EQ(ctx.text_width_px("A\u2206\u2212B\u2014", 16.0f),
                   ctx.text_width_px("AAAAA", 16.0f));  // mono: byte-insensitive width
}

// -------------------------------------------------------------- primitives

AEGIS_TEST(ui, "fill_rect emits one rect and zero-size rects are dropped") {
    UiContext ctx{test_font()};
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
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.rect_outline(0.0f, 0.0f, 100.0f, 50.0f, rgb(1.0f, 1.0f, 1.0f), 2.0f);
    AEGIS_CHECK_EQ(ctx.items().size(), 4u);
    AEGIS_CHECK(ctx.items()[0].rect.h == 2.0f);              // top
    AEGIS_CHECK(ctx.items()[1].rect.h == 2.0f);              // bottom
    AEGIS_CHECK(ctx.items()[2].rect.w == 2.0f);              // left
    AEGIS_CHECK(ctx.items()[3].rect.w == 2.0f);              // right
}

AEGIS_TEST(ui, "text emits one glyph per printable character with mono advance") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.text(100.0f, 50.0f, 16.0f, "AB", rgb(1.0f, 1.0f, 1.0f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 2u);
    AEGIS_CHECK_EQ(items[0].kind, UiDrawItem::Glyph);
    const FontAtlas& font = test_font();
    const core::f32 advance = ctx.text_width_px("A", 16.0f);
    const core::f32 expected = advance + bearing_px(font, 'B', 16.0f) - bearing_px(font, 'A', 16.0f);
    AEGIS_CHECK(std::fabs(items[1].rect.x - items[0].rect.x - expected) < 1e-4f);
    AEGIS_CHECK(items[0].rect.w > 0.0f && items[0].rect.h > 0.0f);          // real bitmap cells
    AEGIS_CHECK(items[0].uv[2] > items[0].uv[0] && items[0].uv[3] > items[0].uv[1]);  // valid cell UVs
}

AEGIS_TEST(ui, "center and right aligned text shift the cursor") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.text(200.0f, 0.0f, 16.0f, "AB", rgb(1.0f, 1.0f, 1.0f), TextAlign::Center);
    const core::f32 expected_center = 200.0f - ctx.text_width_px("AB", 16.0f) * 0.5f;
    AEGIS_CHECK(std::fabs(ctx.items()[0].rect.x - (expected_center + bearing_px(test_font(), 'A', 16.0f))) < 1e-4f);

    UiContext ctx2{test_font()};
    ctx2.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx2.text(200.0f, 0.0f, 16.0f, "AB", rgb(1.0f, 1.0f, 1.0f), TextAlign::Right);
    const core::f32 expected_right = 200.0f - ctx2.text_width_px("AB", 16.0f);
    AEGIS_CHECK(std::fabs(ctx2.items()[0].rect.x - (expected_right + bearing_px(test_font(), 'A', 16.0f))) < 1e-4f);
}

AEGIS_TEST(ui, "unsupported bytes fall back to '?' and still advance the pen") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    const char raw[] = {'A', static_cast<char>(0xC3), 'B', '\0'};
    ctx.text(0.0f, 0.0f, 16.0f, raw, rgb(1.0f, 1.0f, 1.0f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 3u);  // 'A', fallback '?', 'B'
    const core::f32 advance = ctx.text_width_px("A", 16.0f);
    const core::f32 expected = 2.0f * advance + bearing_px(test_font(), 'B', 16.0f);
    AEGIS_CHECK(std::fabs(items[2].rect.x - expected) < 1e-4f);
}

AEGIS_TEST(ui, "panel emits shadow + fill + outline") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(500.0f, 500.0f), 800.0f, 600.0f);
    ctx.panel(10.0f, 10.0f, 100.0f, 80.0f, rgb(0.1f, 0.1f, 0.12f, 0.9f), rgb(0.5f, 0.5f, 0.6f));
    AEGIS_CHECK_EQ(ctx.items().size(), 6u);  // shadow + fill + 4 outline edges
    AEGIS_CHECK(ctx.items()[0].rect.x == 13.0f);  // shadow offset
}

AEGIS_TEST(ui, "the item list resets each frame and preserves issue order") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 100.0f, 100.0f);
    ctx.fill_rect(0.0f, 0.0f, 1.0f, 1.0f, rgb(1.0f, 0.0f, 0.0f));
    ctx.text(0.0f, 5.0f, 8.0f, "A", rgb(0.0f, 1.0f, 0.0f));
    AEGIS_CHECK_EQ(ctx.items().size(), 2u);
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 100.0f, 100.0f);
    AEGIS_CHECK_EQ(ctx.items().size(), 0u);
}

// ----------------------------------------------------------------- widgets

AEGIS_TEST(ui, "button clicks on release over the button exactly once") {
    UiContext ctx{test_font()};
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
    UiContext ctx{test_font()};
    const core::u64 id = UiContext::make_id("launch");
    ctx.begin_frame(press_at(50.0f, 20.0f), 800.0f, 600.0f);
    (void)ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH");
    ctx.begin_frame(release_at(500.0f, 400.0f), 800.0f, 600.0f);
    AEGIS_CHECK(!ctx.button(id, 10.0f, 10.0f, 80.0f, 30.0f, "LAUNCH"));
}

AEGIS_TEST(ui, "button click does not leak into a neighbour (capture is exclusive)") {
    UiContext ctx{test_font()};
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
    UiContext ctx{test_font()};
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
    UiContext ctx{test_font()};
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
    UiContext ctx{test_font()};
    const core::u64 id = UiContext::make_id("throttle");
    ctx.begin_frame(mouse_at(300.0f, 300.0f), 800.0f, 600.0f);  // far from the slider
    const core::f64 value = ctx.slider_h(id, 10.0f, 10.0f, 100.0f, 16.0f, 0.42);
    AEGIS_CHECK(value == 0.42);
}

AEGIS_TEST(ui, "horizontal slider follows the pointer during a drag") {
    UiContext ctx{test_font()};
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
    UiContext ctx{test_font()};
    const core::u64 id = UiContext::make_id("throttle_v");
    ctx.begin_frame(press_at(20.0f, 104.0f), 800.0f, 600.0f);  // near the bottom
    core::f64 value = ctx.slider_v(id, 10.0f, 10.0f, 20.0f, 100.0f, 0.5);
    AEGIS_CHECK(value < 0.1);
    ctx.begin_frame(hold_at(20.0f, 12.0f), 800.0f, 600.0f);  // near the top
    value = ctx.slider_v(id, 10.0f, 10.0f, 20.0f, 100.0f, value);
    AEGIS_CHECK(value > 0.9);
}

AEGIS_TEST(ui, "bar clamps its fill and emits no fill at zero ratio") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.bar(10.0f, 10.0f, 100.0f, 10.0f, 1.7, rgb(0.2f, 0.9f, 0.2f), rgb(0.1f, 0.1f, 0.1f),
            rgb(0.5f, 0.5f, 0.5f));
    const auto& items = ctx.items();
    AEGIS_CHECK_EQ(items.size(), 6u);  // track + fill + 4 outline edges
    AEGIS_CHECK(std::fabs(items[1].rect.w - 98.0f) < 1e-4f);  // fill spans (w - 2*1) * 1.0

    UiContext ctx2{test_font()};
    ctx2.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx2.bar(10.0f, 10.0f, 100.0f, 10.0f, 0.0, rgb(1.0f, 1.0f, 1.0f), rgb(0.1f, 0.1f, 0.1f),
             rgb(0.5f, 0.5f, 0.5f));
    AEGIS_CHECK_EQ(ctx2.items().size(), 5u);  // track + 4 outline edges, no fill
}

AEGIS_TEST(ui, "ui is deterministic: identical input sequences produce identical items") {
    UiContext ctx_a{test_font()}, ctx_b{test_font()};
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
            AEGIS_CHECK(ctx_a.items()[i].uv == ctx_b.items()[i].uv);
        }
    }
}

AEGIS_TEST(ui, "panel_frame emits panel + one accent rect per tick arm") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.panel_frame(10.0f, 10.0f, 100.0f, 80.0f, rgb(0.1f, 0.1f, 0.12f, 0.9f), rgb(0.5f, 0.5f, 0.6f),
                    rgb(0.35f, 0.85f, 1.0f, 0.8f));
    // panel() = shadow + fill + 4 border edges; 8 tick arms on top of that.
    AEGIS_CHECK_EQ(ctx.items().size(), 14u);
    AEGIS_CHECK(ctx.items().back().color.b > 0.9f);  // accent color wins on the last arm
}

AEGIS_TEST(ui, "press_button interacts but emits no items") {
    UiContext ctx{test_font()};
    const core::u64 id = UiContext::make_id("press_target");
    // press inside, release inside -> click
    ctx.begin_frame(press_at(30.0f, 25.0f), 640.0f, 480.0f);
    AEGIS_CHECK(!ctx.press_button(id, 10.0f, 10.0f, 80.0f, 30.0f));
    AEGIS_CHECK_EQ(ctx.items().size(), 0u);
    // release still inside -> completes the click, still no emission
    UiInput release = mouse_at(30.0f, 25.0f);
    release.mouse_down = false;
    release.mouse_released = true;
    ctx.begin_frame(release, 640.0f, 480.0f);
    AEGIS_CHECK(ctx.press_button(id, 10.0f, 10.0f, 80.0f, 30.0f));
    AEGIS_CHECK_EQ(ctx.items().size(), 0u);
}

AEGIS_TEST(ui, "divider emits a single 1px rect") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 800.0f, 600.0f);
    ctx.divider(20.0f, 50.0f, 120.0f, rgb(0.3f, 0.35f, 0.45f, 0.9f));
    AEGIS_CHECK_EQ(ctx.items().size(), 1u);
    AEGIS_CHECK(ctx.items()[0].rect.h == 1.0f && ctx.items()[0].rect.w == 120.0f);
}

AEGIS_TEST(ui, "viewport is exposed and color helpers are constexpr") {
    UiContext ctx{test_font()};
    ctx.begin_frame(mouse_at(0.0f, 0.0f), 123.0f, 456.0f);
    AEGIS_CHECK(ctx.viewport_width() == 123.0f && ctx.viewport_height() == 456.0f);
    constexpr UiColor c = rgb(0.1f, 0.2f, 0.3f, 0.9f);  // rgb() is constexpr
    AEGIS_CHECK(color_near(c, rgb(0.1f, 0.2f, 0.3f, 0.9f), 0.0f));
}
