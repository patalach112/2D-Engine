// Aegis2D — cinematic camera rig for the flight view (Milestone 9).
//
// Wraps Camera2D with critically-damped-ish ease-out follow (exponential
// approach) and a wheel-driven zoom with its own easing. f32 zoom, f64
// targets — the integrator keeps long-range stability while zooming between
// pad close-up (tens of px/m) and orbit overview (1e-4 px/m).
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>
#include <aegis/renderer2d/camera.hpp>

namespace aegis::render {

class AEGIS_API CameraRig {
public:
    CameraRig() noexcept;

    [[nodiscard]] aegis::renderer2d::Camera2D& camera() noexcept { return camera_; }
    [[nodiscard]] const aegis::renderer2d::Camera2D& camera() const noexcept { return camera_; }

    /// Ask the rig to frame (x, y); the camera glides there over ~1/rate seconds.
    void set_target(core::f64 world_x, core::f64 world_y) noexcept;
    /// Multiplicative wheel step; clamped to the rig's zoom limits.
    void zoom_wheel(core::f64 wheel_notches) noexcept;
    /// Absolute zoom target in px per metre; clamped.
    void set_zoom(core::f32 zoom) noexcept;
    /// Snap instantly (scene switches, restarts).
    void snap(core::f64 world_x, core::f64 world_y, core::f32 zoom) noexcept;

    /// Advance the easing. `dt_seconds` must be the *unclamped* frame delta.
    void update(core::f64 dt_seconds) noexcept;

    void set_follow_rate(core::f32 per_second) noexcept { follow_rate_ = per_second; }
    void set_zoom_rate(core::f32 per_second) noexcept { zoom_rate_ = per_second; }

private:
    aegis::renderer2d::Camera2D camera_;
    core::f64 target_x_ = 0.0;
    core::f64 target_y_ = 0.0;
    core::f32 target_zoom_ = 40.0f;
    core::f32 follow_rate_ = 3.0f;
    core::f32 zoom_rate_ = 7.0f;
};

}  // namespace aegis::render
