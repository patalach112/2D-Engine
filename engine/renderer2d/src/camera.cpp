#include <aegis/renderer2d/camera.hpp>

#include <aegis/renderer2d/projection.hpp>

#include <algorithm>
#include <cmath>

namespace aegis::renderer2d {

namespace {

constexpr core::f32 kMinViewportPx = 1.0f;
constexpr core::f32 kMinZoom = 1e-6f;

} // namespace

Camera2D::Camera2D(const Camera2DConfig& config)
    : viewport_width_(std::max(config.viewport_width, kMinViewportPx)),
      viewport_height_(std::max(config.viewport_height, kMinViewportPx)),
      zoom_(config.initial_zoom),
      min_zoom_(config.min_zoom),
      max_zoom_(config.max_zoom) {
    // Normalize limits and clamp the initial zoom into them.
    if (min_zoom_ > max_zoom_) {
        std::swap(min_zoom_, max_zoom_);
    }
    min_zoom_ = std::max(min_zoom_, kMinZoom);
    zoom_ = std::clamp(zoom_, min_zoom_, max_zoom_);
}

void Camera2D::set_viewport(const core::f32 width_px, const core::f32 height_px) {
    viewport_width_ = std::max(width_px, kMinViewportPx);
    viewport_height_ = std::max(height_px, kMinViewportPx);
}

void Camera2D::set_position(const core::f32 x, const core::f32 y) {
    position_x_ = x;
    position_y_ = y;
}

void Camera2D::move(const core::f32 dx, const core::f32 dy) {
    position_x_ += dx;
    position_y_ += dy;
}

void Camera2D::pan_pixels(const core::f32 dx_px, const core::f32 dy_px) {
    // Screen +x is world +x; screen +y (down) is world -y.
    position_x_ -= dx_px / zoom_;
    position_y_ += dy_px / zoom_;
}

void Camera2D::set_zoom(const core::f32 zoom) {
    zoom_ = std::clamp(zoom, min_zoom_, max_zoom_);
}

void Camera2D::set_zoom_limits(const core::f32 min_zoom, const core::f32 max_zoom) {
    min_zoom_ = std::max(min_zoom, kMinZoom);
    max_zoom_ = std::max(max_zoom, min_zoom_);
    zoom_ = std::clamp(zoom_, min_zoom_, max_zoom_);
}

void Camera2D::zoom_at(const core::f32 screen_x, const core::f32 screen_y, const core::f32 factor) {
    if (factor <= 0.0f || !std::isfinite(factor)) {
        return;
    }
    const auto world_before = screen_to_world(screen_x, screen_y);
    zoom_ = std::clamp(zoom_ * factor, min_zoom_, max_zoom_);
    const auto world_after = screen_to_world(screen_x, screen_y);
    position_x_ += world_before[0] - world_after[0];
    position_y_ += world_before[1] - world_after[1];
}

std::array<core::f32, 2> Camera2D::world_to_screen(const core::f32 wx, const core::f32 wy) const noexcept {
    const core::f32 sx = (wx - position_x_) * zoom_ + viewport_width_ * 0.5f;
    const core::f32 sy = viewport_height_ * 0.5f - (wy - position_y_) * zoom_;
    return {sx, sy};
}

std::array<core::f32, 2> Camera2D::screen_to_world(const core::f32 sx, const core::f32 sy) const noexcept {
    const core::f32 wx = (sx - viewport_width_ * 0.5f) / zoom_ + position_x_;
    const core::f32 wy = (viewport_height_ * 0.5f - sy) / zoom_ + position_y_;
    return {wx, wy};
}

std::array<core::f32, 16> Camera2D::view_projection() const noexcept {
    const core::f32 half_w = visible_world_width() * 0.5f;
    const core::f32 half_h = visible_world_height() * 0.5f;
    return make_orthographic(position_x_ - half_w, position_x_ + half_w,
                             position_y_ - half_h, position_y_ + half_h);
}

}  // namespace aegis::renderer2d
