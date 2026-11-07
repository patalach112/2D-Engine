// Aegis2D — physics types for flight dynamics (Milestone 8).
//
// SI units inside world units (1 wu == 1 m): masses are M7 mass units (kg),
// thrust in newtons, time in seconds, mu in m^3/s^2. Positions are absolute,
// planet-centred, in the world XY plane (y-up, same frame as M4/M5/M7).
// Flight state is f64 — integration accuracy matters more than bandwidth for a
// single vehicle.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/parts/vehicle_assembly.hpp>

#include <cmath>

namespace aegis::physics {

using core::f32;
using core::f64;
using core::u8;
using core::u32;

inline constexpr f64 kStandardGravity = 9.80665;

/// Minimal f64 2D vector (the engine stays GLM-free).
struct Vec2d {
    f64 x = 0.0, y = 0.0;

    constexpr Vec2d operator+(const Vec2d o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2d operator-(const Vec2d o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2d operator-() const noexcept { return {-x, -y}; }
    constexpr Vec2d operator*(const f64 s) const noexcept { return {x * s, y * s}; }
    constexpr Vec2d& operator+=(const Vec2d o) noexcept { x += o.x; y += o.y; return *this; }
    constexpr Vec2d& operator-=(const Vec2d o) noexcept { x -= o.x; y -= o.y; return *this; }
    constexpr bool operator==(const Vec2d& o) const noexcept = default;

    [[nodiscard]] constexpr f64 dot(const Vec2d o) const noexcept { return x * o.x + y * o.y; }
    [[nodiscard]] constexpr f64 cross(const Vec2d o) const noexcept { return x * o.y - y * o.x; }
    [[nodiscard]] constexpr f64 length_sq() const noexcept { return dot(*this); }
    [[nodiscard]] f64 length() const noexcept { return std::hypot(x, y); }
    [[nodiscard]] Vec2d normalized() const noexcept {
        const f64 len = length();
        return len > 0.0 ? Vec2d{x / len, y / len} : Vec2d{};
    }
    [[nodiscard]] f64 angle() const noexcept { return std::atan2(y, x); }

    [[nodiscard]] static Vec2d from_angle(const f64 rad) noexcept {
        return {std::cos(rad), std::sin(rad)};
    }
};

/// A single point-mass planet (patched conics and multi-body dynamics are out
/// of scope for M8). mu == 0 disables gravity (deep-space tests, Δv checks).
struct CelestialBody {
    f64 mu = 3.986004418e14;          ///< m^3/s^2 — Earth default
    f64 radius = 6.371e6;             ///< m
    f64 atmosphere_density0 = 1.225;  ///< kg/m^3 at the surface (0 == vacuum)
    f64 atmosphere_scale_height = 8500.0;      ///< m
    f64 atmosphere_top = 100000.0;    ///< m — density is exactly 0 above this

    [[nodiscard]] f64 surface_gravity() const noexcept {
        return radius > 0.0 ? mu / (radius * radius) : 0.0;
    }
    [[nodiscard]] bool has_atmosphere() const noexcept {
        return atmosphere_density0 > 0.0 && atmosphere_scale_height > 0.0;
    }
};

struct AeroConfig {
    f64 drag_coefficient = 0.35;
    f64 reference_area = 1.0;    ///< m^2 — constant for M8 (auto-estimate deferred)
    f64 weathercock_gain = 0.0;  ///< N·m per unit sin(α) at the surface; 0 disables
    f64 weathercock_arm = 2.0;   ///< m ahead of CoM (weathercock acts forward of CoM)
};

struct RotationConfig {
    f64 control_torque = 0.0;    ///< N·m at full pitch deflection (0 = no authority)
    f64 angular_damping = 0.0;   ///< 1/s — ω decays as exp(-d t)
};

struct ContactConfig {
    f64 impact_threshold = 5.0;  ///< m/s radial — above this a touchdown is an impact
};

struct VehicleConfig {
    CelestialBody body;
    AeroConfig aero;
    RotationConfig rotation;
    ContactConfig contact;

    /// True when the physics flags make sense to integrate with.
    [[nodiscard]] bool is_valid() const noexcept {
        if (body.mu < 0.0 || body.radius <= 0.0) {
            return false;
        }
        if (body.atmosphere_density0 < 0.0 || body.atmosphere_scale_height <= 0.0 ||
            body.atmosphere_top < 0.0) {
            return false;
        }
        return aero.drag_coefficient >= 0.0 && aero.reference_area >= 0.0 &&
               aero.weathercock_arm >= 0.0 && rotation.control_torque >= 0.0 &&
               rotation.angular_damping >= 0.0 && contact.impact_threshold >= 0.0;
    }
};

struct FlightState {
    Vec2d position;            ///< planet-centred, m — the world position of the CoM
    Vec2d velocity;            ///< m/s, inertial (== air-relative; no wind model)
    f64 heading = 1.5707963267948966;  ///< rad — 0 = +x, pi/2 = straight up (y-up)
    f64 angular_velocity = 0.0;        ///< rad/s (positive = CCW)
    f64 time = 0.0;                    ///< s

    friend bool operator==(const FlightState&, const FlightState&) noexcept = default;
};

enum class ContactState : u8 { Airborne, Grounded, Impacted };

enum class FlightEvent : u8 {
    StageSeparated,
    FairingJettisoned,
    EngineIgnited,
    EngineFlameout,
    Touchdown,
    Impact,
};

/// A stage (or booster) that has been detached and is now a ballistic
/// free-flyer: gravity-only integration, no thrust/aero/control, no events.
struct DetachedStage {
    parts::VehicleAssembly assembly;
    FlightState state;
    ContactState contact = ContactState::Airborne;
};

}  // namespace aegis::physics
