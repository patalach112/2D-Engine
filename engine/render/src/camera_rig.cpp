// Aegis2D — eased camera rig implementation (Milestone 9).
#include <aegis/render/camera_rig.hpp>

#include <algorithm>
#include <cmath>

namespace aegis::render {

CameraRig::CameraRig() noexcept
    : camera_([] {
          aegis::renderer2d::Camera2DConfig config{};
          // 1e-5 px/m frames a 1.27e7 m-wide planet at planetary scale; 400 px/m
          // resolves centimetre-scale nozzle bells at the pad.
          config.initial_zoom = 40.0f;
          config.min_zoom = 1.0e-5f;
          config.max_zoom = 400.0f;
          return config;
      }()) {}

void CameraRig::set_target(core::f64 world_x, core::f64 world_y) noexcept {
    target_x_ = world_x;
    target_y_ = world_y;
}

void CameraRig::zoom_wheel(core::f64 wheel_notches) noexcept {
    // 1.22^notch — feels like SFS's chunky zoom steps.
    target_zoom_ = static_cast<core::f32>(static_cast<core::f64>(target_zoom_) *
                                          std::pow(1.22, wheel_notches));
    target_zoom_ = std::clamp(target_zoom_, camera_.min_zoom(), camera_.max_zoom());
}

void CameraRig::set_zoom(const core::f32 zoom) noexcept {
    target_zoom_ = std::clamp(zoom, camera_.min_zoom(), camera_.max_zoom());
}

void CameraRig::snap(const core::f64 world_x, const core::f64 world_y, const core::f32 zoom) noexcept {
    target_x_ = world_x;
    target_y_ = world_y;
    target_zoom_ = std::clamp(zoom, camera_.min_zoom(), camera_.max_zoom());
    camera_.set_position(static_cast<core::f32>(world_x), static_cast<core::f32>(world_y));
    camera_.set_zoom(target_zoom_);
}

void CameraRig::update(core::f64 dt_seconds) noexcept {
    if (dt_seconds <= 0.0) {
        return;
    }
    dt_seconds = std::min(dt_seconds, 0.25);  // clamp hitches; never teleport more than one ease step
    const core::f64 follow = 1.0 - std::exp(-static_cast<core::f64>(follow_rate_) * dt_seconds);
    const core::f64 zoom_ease = 1.0 - std::exp(-static_cast<core::f64>(zoom_rate_) * dt_seconds);
    const core::f64 cx = static_cast<core::f64>(camera_.x()) + (target_x_ - static_cast<core::f64>(camera_.x())) * follow;
    const core::f64 cy = static_cast<core::f64>(camera_.y()) + (target_y_ - static_cast<core::f64>(camera_.y())) * follow;
    camera_.set_position(static_cast<core::f32>(cx), static_cast<core::f32>(cy));
    const core::f32 zoom = static_cast<core::f32>(static_cast<core::f64>(camera_.zoom()) +
                                                  (static_cast<core::f64>(target_zoom_) -
                                                   static_cast<core::f64>(camera_.zoom())) * zoom_ease);
    camera_.set_zoom(zoom);
}

}  // namespace aegis::render
