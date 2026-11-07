// Aegis2D — 2D zoom camera (Milestone 4).
//
// World space is y-up (mathematical convention); screen space is y-down,
// measured in pixels with the origin at the top-left corner.
//
// `zoom` is expressed in pixels per world unit: zoom == 1 means one world
// unit maps to one pixel. `world_per_pixel()` is its inverse and is used to
// keep screen-space element sizes (e.g. line thickness) constant across zoom
// levels.
//
// Vulkan-free: usable from tools and unit tests. view_projection() yields the
// same column-major mat4 convention as make_orthographic() and can be fed
// straight into BatchRenderer::begin_frame().
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

#include <array>

namespace aegis::renderer2d {

struct AEGIS_API Camera2DConfig {
    core::f32 viewport_width = 1280.0f;   ///< pixels
    core::f32 viewport_height = 720.0f;   ///< pixels
    core::f32 initial_zoom = 1.0f;        ///< pixels per world unit
    core::f32 min_zoom = 0.02f;           ///< pixels per world unit
    core::f32 max_zoom = 256.0f;          ///< pixels per world unit
};

class AEGIS_API Camera2D {
public:
    explicit Camera2D(const Camera2DConfig& config = {});

    /// Resizes the viewport (window resize). Values below 1 px are clamped.
    void set_viewport(core::f32 width_px, core::f32 height_px);
    /// The camera looks at `(x, y)` (world units, camera center).
    void set_position(core::f32 x, core::f32 y);
    /// Shifts the camera in world units.
    void move(core::f32 dx, core::f32 dy);
    /// Shifts the camera by screen-pixel amounts; (+x → right, +y → down).
    void pan_pixels(core::f32 dx_px, core::f32 dy_px);
    /// Clamps to [min_zoom, max_zoom].
    void set_zoom(core::f32 zoom);
    /// Sets the allowed zoom range; adjusts the current zoom if needed.
    void set_zoom_limits(core::f32 min_zoom, core::f32 max_zoom);
    /// Multiplies the zoom by `factor`, keeping the world point currently
    /// under (screen_x, screen_y) fixed on screen (cursor-anchored zoom).
    void zoom_at(core::f32 screen_x, core::f32 screen_y, core::f32 factor);

    [[nodiscard]] core::f32 x() const noexcept { return position_x_; }
    [[nodiscard]] core::f32 y() const noexcept { return position_y_; }
    [[nodiscard]] core::f32 zoom() const noexcept { return zoom_; }
    [[nodiscard]] core::f32 min_zoom() const noexcept { return min_zoom_; }
    [[nodiscard]] core::f32 max_zoom() const noexcept { return max_zoom_; }
    [[nodiscard]] core::f32 viewport_width() const noexcept { return viewport_width_; }
    [[nodiscard]] core::f32 viewport_height() const noexcept { return viewport_height_; }

    /// Inverse of zoom: world units per pixel.
    [[nodiscard]] core::f32 world_per_pixel() const noexcept { return 1.0f / zoom_; }
    [[nodiscard]] core::f32 visible_world_width() const noexcept { return viewport_width_ / zoom_; }
    [[nodiscard]] core::f32 visible_world_height() const noexcept { return viewport_height_ / zoom_; }

    /// World (y-up) → screen (y-down, pixels, origin top-left).
    [[nodiscard]] std::array<core::f32, 2> world_to_screen(core::f32 wx, core::f32 wy) const noexcept;
    /// Screen (y-down, pixels, origin top-left) → world (y-up).
    [[nodiscard]] std::array<core::f32, 2> screen_to_world(core::f32 sx, core::f32 sy) const noexcept;

    /// Column-major mat4 for BatchRenderer::begin_frame(). The orthographic
    /// box is centered on the camera with height = visible_world_height() and
    /// width = visible_world_width(); near/far = [-1, 1] mapped to Vulkan
    /// [0, 1] depth by make_orthographic().
    [[nodiscard]] std::array<core::f32, 16> view_projection() const noexcept;

private:
    core::f32 viewport_width_ = 1280.0f;
    core::f32 viewport_height_ = 720.0f;
    core::f32 position_x_ = 0.0f;
    core::f32 position_y_ = 0.0f;
    core::f32 zoom_ = 1.0f;
    core::f32 min_zoom_ = 0.02f;
    core::f32 max_zoom_ = 256.0f;
};

}  // namespace aegis::renderer2d
