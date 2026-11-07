// Aegis2D — physics model kernels (Milestone 8).
#include <aegis/physics/model.hpp>

namespace aegis::physics::model {

Vec2d gravity_acceleration(const CelestialBody& body, const Vec2d& position) noexcept {
    if (body.mu == 0.0) {
        return {};
    }
    const f64 r_sq = position.length_sq();
    if (r_sq <= 0.0) {
        return {};
    }
    const f64 r = std::sqrt(r_sq);
    return position * (-body.mu / (r * r_sq));  // r_vec * -mu / r^3
}

f64 atmosphere_density(const CelestialBody& body, const f64 altitude) noexcept {
    if (altitude >= body.atmosphere_top) {
        return 0.0;
    }
    return body.atmosphere_density0 * std::exp(-altitude / body.atmosphere_scale_height);
}

Vec2d drag_force(const CelestialBody& body, const f64 rho, const Vec2d& velocity,
                 const AeroConfig& aero) noexcept {
    (void)body;
    const f64 speed = velocity.length();
    if (speed <= 0.0 || rho <= 0.0 || aero.drag_coefficient == 0.0 || aero.reference_area == 0.0) {
        return {};
    }
    const f64 magnitude = 0.5 * rho * speed * speed * aero.drag_coefficient * aero.reference_area;
    return velocity * (-magnitude / speed);
}

f64 wrap_pi(f64 angle) noexcept {
    constexpr f64 pi = 3.14159265358979323846;
    constexpr f64 two_pi = 2.0 * pi;
    angle = std::fmod(angle, two_pi);
    if (angle <= -pi) {
        angle += two_pi;
    } else if (angle > pi) {
        angle -= two_pi;
    }
    return angle;
}

Vec2d engine_thrust_local_axis(const parts::PartTemplate& engine_template, const u8 part_rotation) noexcept {
    for (const parts::NodeDef& node : engine_template.nodes) {
        if (node.type != parts::NodeType::Top) {
            continue;
        }
        const auto [pos, normal] =
            parts::transform_node(node, engine_template.w, engine_template.h, part_rotation);
        (void)pos;
        return Vec2d{static_cast<f64>(normal[0]), static_cast<f64>(normal[1])};
    }
    return Vec2d{0.0, 1.0};
}

}  // namespace aegis::physics::model
