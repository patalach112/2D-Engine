// Aegis2D — HUD draw-list -> world-space bridge (Milestone 9).
#include <aegis/render/ui_bridge.hpp>

#include <aegis/renderer2d/batch_renderer.hpp>
#include <aegis/renderer2d/camera.hpp>
#include <aegis/renderer2d/glyph_instance.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>
#include <aegis/ui/ui_context.hpp>

namespace aegis::render {

void submit_ui(aegis::renderer2d::BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
               const aegis::ui::UiContext& context) noexcept {
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 vw = camera.viewport_width();
    const core::f32 vh = camera.viewport_height();
    const core::f32 cx = camera.x();
    const core::f32 cy = camera.y();

    for (const ui::UiDrawItem& item : context.items()) {
        // Screen pixels (top-left origin) -> world (y-up), camera-anchored.
        const core::f32 center_px_x = item.rect.x + item.rect.w * 0.5f;
        const core::f32 center_px_y = item.rect.y + item.rect.h * 0.5f;
        const core::f32 wx = cx + (center_px_x - vw * 0.5f) * wpp;
        const core::f32 wy = cy + (vh * 0.5f - center_px_y) * wpp;
        const core::f32 w = item.rect.w * wpp;
        const core::f32 h = item.rect.h * wpp;

        if (item.kind == ui::UiDrawItem::Rect) {
            aegis::renderer2d::SpriteInstance instance{};
            instance.x = wx;
            instance.y = wy;
            instance.width = w;
            instance.height = h;
            instance.r = item.color.r;
            instance.g = item.color.g;
            instance.b = item.color.b;
            instance.a = item.color.a;
            instance.rotation = 0.0f;
            instance.softness = 0.0f;
            renderer.draw_ui(instance);
            continue;
        }
        aegis::renderer2d::GlyphInstance glyph = aegis::renderer2d::make_glyph(
            wx, wy, w, h, item.uv[0], item.uv[1], item.uv[2], item.uv[3],
            item.color.r, item.color.g, item.color.b, item.color.a);
        renderer.draw_glyph(glyph);
    }
}

}  // namespace aegis::render
