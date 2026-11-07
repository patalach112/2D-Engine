// Aegis2D — unit tests for the 2D zoom camera (Milestone 4).
#include <aegis/renderer2d/camera.hpp>

#include <aegis/renderer2d/line_instance.hpp>
#include <aegis/renderer2d/projection.hpp>
#include <aegis/renderer2d/sprite_instance.hpp>

#include "aegis_test.hpp"

#include <array>
#include <cmath>
#include <string>

using aegis::core::f32;
using aegis::renderer2d::Camera2D;
using aegis::renderer2d::Camera2DConfig;

namespace {

void expect_near(const float actual, const float expected, const float eps, const char* expr) {
    ++::aegis::test::check_count();
    const float delta = std::fabs(actual - expected);
    if (delta > eps) {
        ::aegis::test::report_failure(__FILE__, __LINE__,
                                      std::string("expect_near failed: ") + expr +
                                          " (got " + std::to_string(actual) + ", expected " +
                                          std::to_string(expected) + ", delta " + std::to_string(delta) + ")");
    }
}

}  // namespace

AEGIS_TEST(camera, "world origin maps to viewport center at the default pose") {
    Camera2D camera;  // 1280x720, zoom 1, at (0, 0)
    const auto screen = camera.world_to_screen(0.0f, 0.0f);
    expect_near(screen[0], 640.0f, 1e-4f, "origin -> screen x = vw/2");
    expect_near(screen[1], 360.0f, 1e-4f, "origin -> screen y = vh/2");

    const auto world = camera.screen_to_world(640.0f, 360.0f);
    expect_near(world[0], 0.0f, 1e-6f, "screen center -> world x = 0");
    expect_near(world[1], 0.0f, 1e-6f, "screen center -> world y = 0");
}

AEGIS_TEST(camera, "world is y-up, screen is y-down") {
    Camera2D camera;
    const auto up = camera.world_to_screen(0.0f, 10.0f);
    expect_near(up[1], 360.0f - 10.0f, 1e-4f, "world +y maps to smaller screen y");
    const auto down = camera.world_to_screen(0.0f, -10.0f);
    expect_near(down[1], 360.0f + 10.0f, 1e-4f, "world -y maps to larger screen y");
}

AEGIS_TEST(camera, "world<->screen round trip is stable") {
    Camera2DConfig config;
    config.viewport_width = 1600.0f;
    config.viewport_height = 900.0f;
    config.initial_zoom = 2.35f;
    Camera2D camera(config);
    camera.set_position(123.4f, -98.7f);

    for (const auto& [wx, wy] : {std::array<f32, 2>{0.0f, 0.0f}, {350.5f, 12.25f}, {-777.0f, 404.0f}}) {
        const auto screen = camera.world_to_screen(wx, wy);
        const auto back = camera.screen_to_world(screen[0], screen[1]);
        expect_near(back[0], wx, 1e-2f, "round trip x");
        expect_near(back[1], wy, 1e-2f, "round trip y");
    }
}

AEGIS_TEST(camera, "zoom_at keeps the world point under the cursor fixed") {
    Camera2DConfig config;
    config.viewport_width = 1280.0f;
    config.viewport_height = 720.0f;
    config.initial_zoom = 1.6f;
    Camera2D camera(config);
    camera.set_position(37.0f, 21.0f);

    for (const auto& [cx, cy] : {std::array<f32, 2>{100.0f, 80.0f}, {640.0f, 360.0f}, {1200.0f, 700.0f}}) {
        const auto before = camera.screen_to_world(cx, cy);
        camera.zoom_at(cx, cy, 1.5f);
        const auto after = camera.screen_to_world(cx, cy);
        expect_near(after[0], before[0], 1e-3f, "anchor x stable after zoom");
        expect_near(after[1], before[1], 1e-3f, "anchor y stable after zoom");
        camera.zoom_at(cx, cy, 1.0f / 1.5f);  // revert zoom
    }
}

AEGIS_TEST(camera, "zoom clamps to [min_zoom, max_zoom]") {
    Camera2DConfig config;
    config.initial_zoom = 1.0f;
    config.min_zoom = 0.5f;
    config.max_zoom = 4.0f;
    Camera2D camera(config);

    camera.set_zoom(100.0f);
    expect_near(camera.zoom(), 4.0f, 1e-7f, "zoom clamped to max");
    camera.set_zoom(0.001f);
    expect_near(camera.zoom(), 0.5f, 1e-7f, "zoom clamped to min");

    camera.set_zoom_limits(2.0f, 10.0f);  // raises min; current zoom must follow
    expect_near(camera.zoom(), 2.0f, 1e-7f, "zoom pulled into the new range");
    expect_near(camera.min_zoom(), 2.0f, 1e-7f, "min limit updated");
    expect_near(camera.max_zoom(), 10.0f, 1e-7f, "max limit updated");
}

AEGIS_TEST(camera, "pan_pixels uses the grab-the-world convention") {
    Camera2D camera;  // zoom 1
    camera.pan_pixels(100.0f, 0.0f);   // content dragged right -> camera moves left
    expect_near(camera.x(), -100.0f, 1e-5f, "pan +x_px moves camera -x");
    camera.pan_pixels(0.0f, 50.0f);    // dragged down -> camera moves up in world
    expect_near(camera.y(), 50.0f, 1e-5f, "pan +y_px moves camera +y");
    expect_near(camera.x(), -100.0f, 1e-5f, "x untouched by y pan");
}

AEGIS_TEST(camera, "visible_world_* tracks viewport and zoom") {
    Camera2DConfig config;
    config.viewport_width = 1000.0f;
    config.viewport_height = 500.0f;
    config.initial_zoom = 2.0f;
    Camera2D camera(config);

    expect_near(camera.visible_world_width(), 500.0f, 1e-5f, "visible width = vw / zoom");
    expect_near(camera.visible_world_height(), 250.0f, 1e-5f, "visible height = vh / zoom");
    expect_near(camera.world_per_pixel(), 0.5f, 1e-7f, "world_per_pixel = 1/zoom");

    camera.set_viewport(2000.0f, 1000.0f);
    expect_near(camera.visible_world_width(), 1000.0f, 1e-5f, "visible width after resize");
    expect_near(camera.visible_world_height(), 500.0f, 1e-5f, "visible height after resize");
}

AEGIS_TEST(camera, "view_projection embeds camera position and visible size") {
    Camera2DConfig config;
    config.viewport_width = 1280.0f;
    config.viewport_height = 720.0f;
    config.initial_zoom = 2.0f;
    Camera2D camera(config);
    camera.set_position(100.0f, 50.0f);

    const auto m = camera.view_projection();
    // visible_world_width = 640, visible_world_height = 360;
    // bounds: [100-320, 100+320] x [50-180, 50+180]
    expect_near(m[0], 2.0f / 640.0f, 1e-7f, "scale x");
    expect_near(m[5], -2.0f / 360.0f, 1e-7f, "scale y (Vulkan NDC y-down)");
    expect_near(m[12], -0.3125f, 1e-6f, "translate x = -(right+left)/(right-left)");
    expect_near(m[13], 0.2777778f, 1e-5f, "translate y = (top+bottom)/(top-bottom)");
}

AEGIS_TEST(camera, "line instance is bit-compatible with sprite instance") {
    AEGIS_CHECK_EQ(sizeof(aegis::renderer2d::LineInstance), sizeof(aegis::renderer2d::SpriteInstance));
    AEGIS_CHECK_EQ(sizeof(aegis::renderer2d::LineInstance), std::size_t{40});
}
