// Aegis2D — planet, atmosphere, sky and terrain (Milestone 9).
#include <aegis/render/planet_renderer.hpp>

#include <aegis/renderer2d/sprite_instance.hpp>

#include <algorithm>
#include <cmath>

using aegis::renderer2d::SpriteInstance;

namespace aegis::render {
namespace {

constexpr core::f32 kSkyBands = 24.0f;

std::array<core::f32, 4> mix4(const std::array<core::f32, 4> a, const std::array<core::f32, 4> b,
                              const core::f32 t) noexcept {
    std::array<core::f32, 4> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        out[i] = a[i] + (b[i] - a[i]) * t;
    }
    return out;
}

core::f32 hash01(const core::u32 a, const core::u32 b) noexcept {
    core::u32 h = 2166136261u;
    h ^= a;
    h *= 16777619u;
    h ^= b;
    h *= 16777619u;
    return static_cast<core::f32>(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

SpriteInstance sprite(const core::f32 x, const core::f32 y, const core::f32 w, const core::f32 h,
                      const std::array<core::f32, 4> color, const core::f32 rotation = 0.0f,
                      const core::f32 softness = 0.0f) noexcept {
    SpriteInstance instance{};
    instance.x = x;
    instance.y = y;
    instance.width = w;
    instance.height = h;
    instance.r = color[0];
    instance.g = color[1];
    instance.b = color[2];
    instance.a = color[3];
    instance.rotation = rotation;
    instance.softness = softness;
    return instance;
}

}  // namespace

void draw_sky(aegis::renderer2d::BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
              const core::f32 altitude_fraction, const core::u32 star_seed) noexcept {
    const core::f32 alt = std::clamp(altitude_fraction, 0.0f, 1.0f);
    const core::f32 vw = camera.visible_world_width();
    const core::f32 vh = camera.visible_world_height();
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 x = camera.x();
    const core::f32 top = camera.y() + vh * 0.5f;
    const core::f32 band = vh / kSkyBands;

    const std::array<core::f32, 4> sea_zenith{0.055f, 0.11f, 0.23f, 1.0f};
    const std::array<core::f32, 4> sea_horizon{0.52f, 0.65f, 0.76f, 1.0f};
    const std::array<core::f32, 4> high_zenith{0.004f, 0.006f, 0.016f, 1.0f};
    const std::array<core::f32, 4> high_horizon{0.045f, 0.065f, 0.115f, 1.0f};
    const auto zenith = mix4(sea_zenith, high_zenith, alt);
    const auto horizon = mix4(sea_horizon, high_horizon, alt);

    for (core::u32 i = 0; i < static_cast<core::u32>(kSkyBands); ++i) {
        const core::f32 t = (static_cast<core::f32>(i) + 0.5f) / kSkyBands;  // 0 == zenith
        const core::f32 curve = std::pow(t, 1.65f);
        const auto color = mix4(zenith, horizon, curve);
        const core::f32 cy = top - (static_cast<core::f32>(i) + 0.5f) * band;
        // Overlap bands by ~2px so f32 rounding never opens a seam.
        renderer.draw_sprite(sprite(x, cy, vw + 4.0f * wpp, band + 2.0f * wpp, color));
    }

    // Procedural starfield: stable per star index, fades in with altitude.
    const core::f32 star_alpha = std::clamp((alt - 0.22f) * 1.6f, 0.0f, 1.0f);
    if (star_alpha > 0.0f) {
        for (core::u32 star = 0; star < 72; ++star) {
            const core::f32 sx = x + (hash01(star, star_seed) - 0.5f) * vw;
            const core::f32 sy = top - hash01(star, star_seed ^ 0x9E37u) * vh * 0.78f;
            const core::f32 size = (0.5f + 1.5f * hash01(star, 0x51FAu)) * 2.2f * wpp;
            const core::f32 a = star_alpha * (0.35f + 0.65f * hash01(star, 0xBEEFu));
            renderer.draw_sprite(sprite(sx, sy, size, size,
                                        std::array<core::f32, 4>{0.92f, 0.95f, 1.0f, a}, 0.0f, -1.0f));
        }
    }
}

void draw_planet(aegis::renderer2d::BatchRenderer& renderer, const core::f64 center_x, const core::f64 center_y,
                 const PlanetStyle& style) noexcept {
    const core::f32 cx = static_cast<core::f32>(center_x);
    const core::f32 cy = static_cast<core::f32>(center_y);
    const core::f32 r = static_cast<core::f32>(style.radius_m);

    // Far atmosphere glow (under the body) — three stacked annuli discs.
    const std::array<std::pair<core::f32, core::f32>, 3> under_annuli{{{1.46f, 0.07f}, {1.27f, 0.15f}, {1.15f, 0.30f}}};
    for (const auto& [scale, alpha] : under_annuli) {
        auto color = style.atmosphere;
        color[3] = alpha;
        renderer.draw_sprite(sprite(cx, cy, 2.0f * r * scale, 2.0f * r * scale, color, 0.0f, -1.0f));
    }
    // Ocean rim: single oversize disc left visible around the land disc.
    auto ocean = style.ocean_limb;
    renderer.draw_sprite(sprite(cx, cy, 2.0f * r * 1.045f, 2.0f * r * 1.045f, ocean, 0.0f, -1.0f));
    // Land disc.
    renderer.draw_sprite(sprite(cx, cy, 2.0f * r * 0.94f, 2.0f * r * 0.94f, style.land, 0.0f, -1.0f));
    // Shaded hemisphere: smaller disc pushed toward the anti-light side,
    // stays fully inside the land disc.
    auto shade = style.land_shade;
    shade[3] = 0.62f;
    renderer.draw_sprite(sprite(cx + 0.24f * r, cy - 0.24f * r, 2.0f * r * 0.72f, 2.0f * r * 0.72f, shade, 0.0f, -1.0f));
    // Inner atmosphere rim (over the body) — thin bright limb.
    auto rim = style.atmosphere;
    rim[3] = 0.55f;
    renderer.draw_sprite(sprite(cx, cy, 2.0f * r * 1.03f, 2.0f * r * 1.03f, rim, 0.0f, -1.0f));
    // Re-cover the interior of that rim with the surface so only the edge glows.
    renderer.draw_sprite(sprite(cx, cy, 2.0f * r * 0.90f, 2.0f * r * 0.90f, style.land, 0.0f, -1.0f));
    renderer.draw_sprite(sprite(cx + 0.24f * r, cy - 0.24f * r, 2.0f * r * 0.69f, 2.0f * r * 0.69f, shade, 0.0f, -1.0f));
}

void draw_ground(aegis::renderer2d::BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera) noexcept {
    const core::f32 vw = camera.visible_world_width();
    const core::f32 vh = camera.visible_world_height();
    const core::f32 wpp = camera.world_per_pixel();
    const core::f32 x = camera.x();
    // Only when the horizon is visible (view reaches y <= 0).
    const core::f32 bottom = camera.y() - vh * 0.5f;
    if (bottom >= 0.0f) {
        return;
    }
    const core::f32 width = vw + 4.0f * wpp;
    const core::f32 grass = std::min(6.0f, -bottom);
    renderer.draw_sprite(sprite(x, -grass * 0.5f, width, grass + 2.0f * wpp, {0.30f, 0.42f, 0.22f, 1.0f}));
    if (-bottom > grass) {
        renderer.draw_sprite(sprite(x, -grass - (-bottom - grass) * 0.5f, width, (-bottom - grass) + 2.0f * wpp,
                                    {0.36f, 0.29f, 0.20f, 1.0f}));
    }
}

void draw_launch_mount(aegis::renderer2d::BatchRenderer& renderer) noexcept {
    // Concrete pad slab + dark mount pedestal — silhouettes at the origin.
    renderer.draw_sprite(sprite(0.0f, 0.25f, 14.0f, 0.5f, {0.45f, 0.45f, 0.47f, 1.0f}));   // slab
    renderer.draw_sprite(sprite(0.0f, 0.75f, 4.2f, 0.6f, {0.22f, 0.22f, 0.25f, 1.0f}));     // pedestal base
    renderer.draw_sprite(sprite(0.0f, 1.35f, 2.2f, 0.7f, {0.16f, 0.16f, 0.19f, 1.0f}));     // hold-down
    // Service tower silhouette (left of the rocket).
    renderer.draw_sprite(sprite(-3.4f, 5.0f, 0.9f, 10.0f, {0.18f, 0.19f, 0.22f, 1.0f}));    // mast
    renderer.draw_sprite(sprite(-2.1f, 8.6f, 2.6f, 0.5f, {0.18f, 0.19f, 0.22f, 1.0f}));     // arm top
    renderer.draw_sprite(sprite(-2.6f, 4.6f, 1.6f, 0.35f, {0.18f, 0.19f, 0.22f, 1.0f}));    // arm mid
}

}  // namespace aegis::render
