// Aegis2D — VehicleFlight implementation (Milestone 8).
//
// Force model per fixed tick:
//   1. Drain propellant (ṁ = F / (Isp · g₀)) — engines in part-index order,
//      reachable tanks proportional to remaining propellant, one component-wide
//      pool (adjacency includes separator edges until a split cuts them).
//   2. Re-aggregate mass/CoM/Izz lazily via M7 (dirty after drain).
//   3. RK4 on [pos, vel, heading, ω] with gravity, per-engine thrust applied at
//      each engine's grid centre (torque = r × F), control torque, optional
//      weathercock torque, quadratic drag, angular damping.
//   4. Contact constraint: r < R pins the CoM to the surface; radial approach
//      speed beyond ContactConfig.impact_threshold → Impacted (energy-free
//      dead stop, engines cut), otherwise a soft touchdown (radial component
//      removed, tangential preserved).
//   5. Detached stages integrate ballistically (gravity only) and clamp to the
//      surface.
//
// The world position of the vehicle CoM lives in FlightState::position; the
// assembly keeps its internal grid coordinates, so positions of every material
// point are preserved exactly across splits (CoM offset bookkeeping) and across
// rotations (offsets rotate by heading − π/2).
#include <aegis/physics/vehicle_flight.hpp>

#include <aegis/core/assert.hpp>
#include <aegis/physics/model.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace aegis::physics {

using core::ErrorCode;

namespace {

constexpr f64 kHalfPi = 1.5707963267948966;
constexpr f64 kPi = 3.14159265358979323846;

Vec2d rotate_by(const Vec2d v, const f64 angle) noexcept {
    const f64 c = std::cos(angle);
    const f64 s = std::sin(angle);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

bool alive_engine(const parts::VehicleAssembly& asmbl, const parts::PartIndex index,
                  const parts::PartTemplate** out_tmpl) noexcept {
    const parts::Part* p = asmbl.part(index);
    if (!p) {
        return false;
    }
    const parts::PartTemplate* tmpl = parts::find_template(p->tmpl);
    if (!tmpl || tmpl->category != parts::PartCategory::Engine) {
        return false;
    }
    if (out_tmpl) {
        *out_tmpl = tmpl;
    }
    return true;
}

}  // namespace

core::Result<VehicleFlight> VehicleFlight::create(parts::VehicleAssembly assembly,
                                                  parts::StagingList staging,
                                                  const VehicleConfig& config, const Vec2d position,
                                                  const Vec2d velocity, const f64 heading) {
    if (!config.is_valid()) {
        return core::Result<VehicleFlight>::failure(ErrorCode::InvalidArgument,
                                                    "VehicleConfig is invalid (mu/radius/atmosphere/aero/rotation)");
    }
    const std::vector<parts::AssemblyIssue> issues = assembly.validate();
    if (!issues.empty()) {
        const char* what = issues.front().kind == parts::AssemblyIssue::Kind::NoCommandPod
                               ? "no command pod in the assembly"
                               : "unreachable parts in the assembly";
        return core::Result<VehicleFlight>::failure(ErrorCode::InvalidArgument, what);
    }
    if (!std::isfinite(heading) || !std::isfinite(position.x) || !std::isfinite(position.y) ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y)) {
        return core::Result<VehicleFlight>::failure(ErrorCode::InvalidArgument,
                                                    "position/velocity/heading must be finite");
    }

    VehicleFlight flight;
    flight.assembly_ = std::move(assembly);
    flight.staging_ = std::move(staging);
    flight.config_ = config;
    flight.state_.position = position;
    flight.state_.velocity = velocity;
    flight.state_.heading = heading;
    return core::Result<VehicleFlight>::ok(std::move(flight));
}

void VehicleFlight::set_throttle(const f64 throttle) noexcept {
    throttle_ = std::isfinite(throttle) ? std::clamp(throttle, 0.0, 1.0) : 0.0;
}

void VehicleFlight::set_pitch_input(const f64 input) noexcept {
    pitch_input_ = std::isfinite(input) ? std::clamp(input, -1.0, 1.0) : 0.0;
}

void VehicleFlight::take_events(std::vector<FlightEvent>& out) noexcept {
    out.clear();
    out.swap(events_);
}

VehicleFlight::Derivative VehicleFlight::evaluate(const FlightState& s,
                                                  const parts::MassProperties& mp) const noexcept {
    Derivative deriv{};
    deriv.velocity_pos = s.velocity;
    deriv.angular_velocity = s.angular_velocity;

    const f64 rho = model::atmosphere_density(
        config_.body, s.position.length() - config_.body.radius);

    const Vec2d gravity = model::gravity_acceleration(config_.body, s.position);

    Vec2d force = gravity * static_cast<f64>(mp.total_mass);
    f64 torque = 0.0;

    // Thrust per engine (grid frame -> world frame rotation by heading − π/2).
    const f64 theta = s.heading - kHalfPi;
    const bool engines_live = contact_ != ContactState::Impacted;
    for (const parts::PartIndex engine_index : ignited_) {
        if (!engines_live) {
            break;
        }
        if (std::find(flameout_.begin(), flameout_.end(), engine_index) != flameout_.end()) {
            continue;
        }
        const parts::PartTemplate* tmpl = nullptr;
        if (!alive_engine(assembly_, engine_index, &tmpl)) {
            continue;  // detached earlier — its gap is skipped
        }
        const f64 thrust = throttle_ * static_cast<f64>(tmpl->engine.thrust);
        if (thrust <= 0.0) {
            continue;
        }
        const Vec2d local_force_dir = model::engine_thrust_local_axis(*tmpl, assembly_.part(engine_index)->rot);
        const Vec2d world_dir = rotate_by(local_force_dir, theta);
        const std::array<f32, 2> center = assembly_.part_center_wu(engine_index);
        const Vec2d offset_grid{
            static_cast<f64>(center[0]) - static_cast<f64>(mp.center_of_mass[0]),
            static_cast<f64>(center[1]) - static_cast<f64>(mp.center_of_mass[1])};
        const Vec2d offset_world = rotate_by(offset_grid, theta);

        const Vec2d engine_force = world_dir * thrust;
        force += engine_force;
        torque += offset_world.cross(engine_force);
    }

    // Drag — quadratic, opposes the (air-relative == inertial) velocity.
    force += model::drag_force(config_.body, rho, s.velocity, config_.aero);

    // Control torque about the z axis.
    torque += pitch_input_ * config_.rotation.control_torque;

    // Weathercock stability (only with atmosphere and positive gain).
    if (config_.aero.weathercock_gain > 0.0 && config_.body.atmosphere_density0 > 0.0) {
        const f64 speed = s.velocity.length();
        if (speed > 1.0) {
            const f64 alpha = model::wrap_pi(s.velocity.angle() - s.heading);
            torque += config_.aero.weathercock_gain * (rho / config_.body.atmosphere_density0) *
                      std::sin(alpha) * config_.aero.weathercock_arm;
        }
    }

    const f64 mass = static_cast<f64>(mp.total_mass);
    deriv.acceleration = mass > 0.0 ? force * (1.0 / mass) : Vec2d{};

    const f64 izz = static_cast<f64>(mp.inertia_izz);
    deriv.angular_acceleration = (izz > 1e-9) ? torque / izz : 0.0;
    deriv.angular_acceleration -= config_.rotation.angular_damping * s.angular_velocity;

    return deriv;
}

void VehicleFlight::drain_propellant(const f64 dt) noexcept {
    if (throttle_ <= 0.0 || ignited_.empty() || contact_ == ContactState::Impacted) {
        return;
    }
    const f64 g0 = kStandardGravity;

    // Adjacency once for the whole drain pass; sorted edge list, deterministic BFS.
    const std::vector<std::pair<u32, u32>> edges = assembly_.adjacency();
    const std::size_t part_count = assembly_.parts().size();
    std::vector<std::vector<u32>> graph(part_count);
    for (const auto& [a, b] : edges) {
        graph[a].push_back(b);
        graph[b].push_back(a);
    }

    for (const parts::PartIndex engine_index : ignited_) {
        if (std::find(flameout_.begin(), flameout_.end(), engine_index) != flameout_.end()) {
            continue;
        }
        const parts::PartTemplate* tmpl = nullptr;
        if (!alive_engine(assembly_, engine_index, &tmpl)) {
            continue;
        }
        const f64 demand = throttle_ * static_cast<f64>(tmpl->engine.thrust) /
                           (static_cast<f64>(tmpl->engine.isp) * g0);
        f64 needed = demand * dt;
        if (needed <= 0.0) {
            continue;
        }

        // Reachable tanks (BFS), collected in part-index order.
        std::vector<char> visited(part_count, 0);
        std::vector<parts::PartIndex> queue;
        std::vector<parts::PartIndex> tanks;
        visited[engine_index] = 1;
        queue.push_back(engine_index);
        while (!queue.empty()) {
            const u32 current = queue.back();
            queue.pop_back();
            for (const u32 next : graph[current]) {
                if (visited[next]) {
                    continue;
                }
                visited[next] = 1;
                const parts::Part* p = assembly_.part(next);
                if (!p) {
                    continue;
                }
                const parts::PartTemplate* pt = parts::find_template(p->tmpl);
                if (pt && pt->category == parts::PartCategory::FuelTank && p->propellant > 0.0f) {
                    tanks.push_back(next);
                }
                queue.push_back(next);
            }
        }
        std::sort(tanks.begin(), tanks.end());

        f64 available = 0.0;
        for (const parts::PartIndex tank : tanks) {
            available += static_cast<f64>(assembly_.part(tank)->propellant);
        }
        if (available <= 0.0) {
            flameout_.push_back(engine_index);
            events_.push_back(FlightEvent::EngineFlameout);
            continue;
        }

        const f64 to_drain = std::min(needed, available);
        if (to_drain < needed) {
            flameout_.push_back(engine_index);
            events_.push_back(FlightEvent::EngineFlameout);
        }
        for (const parts::PartIndex tank : tanks) {
            if (needed <= 0.0) {
                break;
            }
            const f64 remaining = static_cast<f64>(assembly_.part(tank)->propellant);
            const f64 share = to_drain * (remaining / available);
            if (share <= 0.0) {
                continue;
            }
            const core::Result<f32> drained =
                assembly_.consume_propellant(tank, static_cast<f32>(share));
            if (drained.has_value()) {
                needed -= static_cast<f64>(drained.value());
            }
        }
    }
}

void VehicleFlight::apply_contact_constraint() noexcept {
    const f64 r = state_.position.length();
    const f64 radius = config_.body.radius;
    if (r > radius) {
        contact_ = ContactState::Airborne;
        return;
    }
    if (r <= 0.0) {
        state_.position = Vec2d{0.0, radius};
    } else {
        state_.position = state_.position * (radius / r);
    }
    const Vec2d radial = state_.position.normalized();
    const f64 v_radial = state_.velocity.dot(radial);
    if (v_radial < 0.0) {
        if (std::fabs(v_radial) > config_.contact.impact_threshold) {
            state_.velocity = Vec2d{};
            state_.angular_velocity = 0.0;
            contact_ = ContactState::Impacted;
            events_.push_back(FlightEvent::Impact);
            return;
        }
        state_.velocity = state_.velocity - radial * v_radial;  // keep tangential
    }
    if (contact_ != ContactState::Grounded) {
        contact_ = ContactState::Grounded;
        events_.push_back(FlightEvent::Touchdown);
    }
}

core::Result<void> VehicleFlight::activate_next_stage() {
    const auto props_before = assembly_.mass_properties();
    const Vec2d com_before{static_cast<f64>(props_before.center_of_mass[0]),
                           static_cast<f64>(props_before.center_of_mass[1])};
    const f64 mass_before = static_cast<f64>(props_before.total_mass);  // pre-split total
    const Vec2d velocity_before = state_.velocity;

    core::Result<parts::StageFireResult> result = staging_.activate_next_stage(assembly_);
    if (!result.has_value()) {
        return core::Result<void>::failure(result.error().code, result.error().message);
    }
    parts::StageFireResult fired = std::move(result.value());

    // --- Continuity bookkeeping: points keep their world coordinates --------
    const auto mass_after = assembly_.mass_properties();
    const Vec2d com_after{static_cast<f64>(mass_after.center_of_mass[0]),
                          static_cast<f64>(mass_after.center_of_mass[1])};
    const f64 theta = state_.heading - kHalfPi;
    state_.position += rotate_by(com_after - com_before, theta);

    // --- Build detached stages, applying separation impulses -----------------
    // Each decoupler fire is its own two-body split between the detached piece
    // and EVERYTHING still attached (the adopted vehicle). Impulses are applied
    // sequentially in firing order with a running remaining mass, which keeps
    // linear momentum exactly conserved at every step; a piece carries the
    // vehicle velocity at the moment of its own fire plus its share.
    f64 remaining_mass = mass_before;
    Vec2d vehicle_velocity = velocity_before;

    // Vehicle CoM in grid frame is `com_after`; grid == world frame (no frame
    // translation), so the world position of any grid point g is:
    //   state_.position + R(theta) * (g - com_after)
    separation_velocities_.assign(fired.detached.size(), Vec2d{});
    std::vector<Vec2d> separation_anchors(fired.detached.size(), velocity_before);
    std::vector<char> claimed(fired.detached.size(), 0);
    for (const parts::StageActionItem& item : fired.fired) {
        if (item.action != parts::StageAction::FireDecoupler) {
            continue;
        }
        f64 separation_delta_v = 0.0;
        bool found = false;
        // The decoupler part survives in exactly one component (adopted or
        // detached) — search both; indices are stable everywhere.
        if (const parts::PartTemplate* tmpl = assembly_.part(item.part) ? parts::find_template(assembly_.part(item.part)->tmpl)
                                                                        : nullptr) {
            if (tmpl->category == parts::PartCategory::Decoupler) {
                separation_delta_v = tmpl->decoupler.separation_delta_v;
                found = true;
            }
        }
        if (!found) {
            for (const parts::VehicleAssembly& piece : fired.detached) {
                const parts::Part* p = piece.part(item.part);
                if (!p) {
                    continue;
                }
                const parts::PartTemplate* tmpl = parts::find_template(p->tmpl);
                if (tmpl && tmpl->category == parts::PartCategory::Decoupler) {
                    separation_delta_v = tmpl->decoupler.separation_delta_v;
                    found = true;
                    break;
                }
            }
        }

        // Nearest unclaimed detached piece (by grid CoM distance — deterministic).
        std::size_t best = fired.detached.size();
        f64 best_distance = std::numeric_limits<f64>::infinity();
        for (std::size_t j = 0; j < fired.detached.size(); ++j) {
            if (claimed[j]) {
                continue;
            }
            const auto det_mass = fired.detached[j].mass_properties();
            const Vec2d delta{static_cast<f64>(det_mass.center_of_mass[0]) - com_after.x,
                              static_cast<f64>(det_mass.center_of_mass[1]) - com_after.y};
            const f64 distance = delta.length_sq();
            if (distance < best_distance) {
                best_distance = distance;
                best = j;
            }
        }
        if (best < fired.detached.size()) {
            claimed[best] = 1;
            separation_anchors[best] = vehicle_velocity;  // velocity at the instant of this fire
            const auto det_mass = fired.detached[best].mass_properties();
            const f64 det_mass_value = static_cast<f64>(det_mass.total_mass);
            if (separation_delta_v > 0.0 && found) {
                const Vec2d det_com{static_cast<f64>(det_mass.center_of_mass[0]),
                                    static_cast<f64>(det_mass.center_of_mass[1])};
                Vec2d direction = det_com - com_after;  // works for axial and radial splits
                if (direction.length_sq() <= 1e-18) {
                    direction = Vec2d{0.0, -1.0};
                }
                direction = direction.normalized();
                const f64 combined = remaining_mass;  // total mass before this split
                vehicle_velocity -= direction * (separation_delta_v * det_mass_value / combined);
                separation_velocities_[best] =
                    direction * (separation_delta_v * (combined - det_mass_value) / combined);
            }
            remaining_mass -= det_mass_value;
        }
    }
    state_.velocity = vehicle_velocity;

    for (std::size_t j = 0; j < fired.detached.size(); ++j) {
        parts::VehicleAssembly piece = std::move(fired.detached[j]);
        const auto det_mass = piece.mass_properties();
        const Vec2d det_com{static_cast<f64>(det_mass.center_of_mass[0]),
                            static_cast<f64>(det_mass.center_of_mass[1])};
        DetachedStage detached_stage;
        detached_stage.assembly = std::move(piece);
        detached_stage.state = state_;
        detached_stage.state.position = state_.position + rotate_by(det_com - com_after, theta);
        detached_stage.state.velocity = separation_anchors[j] + separation_velocities_[j];
        detached_stage.contact = ContactState::Airborne;
        detached_.push_back(std::move(detached_stage));
    }
    separation_velocities_.clear();

    // --- Wire actions into flight state --------------------------------------
    for (const parts::StageActionItem& item : fired.fired) {
        switch (item.action) {
            case parts::StageAction::IgniteEngine: {
                const parts::Part* p = assembly_.part(item.part);
                const parts::PartTemplate* tmpl = p ? parts::find_template(p->tmpl) : nullptr;
                if (tmpl && tmpl->category == parts::PartCategory::Engine) {
                    const auto it = std::lower_bound(ignited_.begin(), ignited_.end(), item.part);
                    if (it == ignited_.end() || *it != item.part) {
                        ignited_.insert(it, item.part);
                        events_.push_back(FlightEvent::EngineIgnited);
                    }
                }
                break;
            }
            case parts::StageAction::FireDecoupler:
                events_.push_back(FlightEvent::StageSeparated);
                break;
            case parts::StageAction::JettisonFairing:
                events_.push_back(FlightEvent::FairingJettisoned);
                break;
        }
    }

    return core::Result<void>::ok();
}

void VehicleFlight::tick(const f64 dt) {
    if (contact_ == ContactState::Impacted || dt <= 0.0 || !std::isfinite(dt)) {
        return;
    }

    drain_propellant(dt);

    const parts::MassProperties& mp = assembly_.mass_properties();

    // RK4 over [position, velocity, heading, angular_velocity].
    const FlightState s0 = state_;
    const Derivative k1 = evaluate(s0, mp);

    FlightState s1 = s0;
    s1.position += k1.velocity_pos * (dt * 0.5);
    s1.velocity += k1.acceleration * (dt * 0.5);
    s1.heading += k1.angular_velocity * (dt * 0.5);
    s1.angular_velocity += k1.angular_acceleration * (dt * 0.5);
    const Derivative k2 = evaluate(s1, mp);

    FlightState s2 = s0;
    s2.position += k2.velocity_pos * (dt * 0.5);
    s2.velocity += k2.acceleration * (dt * 0.5);
    s2.heading += k2.angular_velocity * (dt * 0.5);
    s2.angular_velocity += k2.angular_acceleration * (dt * 0.5);
    const Derivative k3 = evaluate(s2, mp);

    FlightState s3 = s0;
    s3.position += k3.velocity_pos * dt;
    s3.velocity += k3.acceleration * dt;
    s3.heading += k3.angular_velocity * dt;
    s3.angular_velocity += k3.angular_acceleration * dt;
    const Derivative k4 = evaluate(s3, mp);

    state_.position += (k1.velocity_pos + (k2.velocity_pos + k3.velocity_pos) * 2.0 + k4.velocity_pos) * (dt / 6.0);
    state_.velocity += (k1.acceleration + (k2.acceleration + k3.acceleration) * 2.0 + k4.acceleration) * (dt / 6.0);
    state_.heading += (k1.angular_velocity + (k2.angular_velocity + k3.angular_velocity) * 2.0 + k4.angular_velocity) * (dt / 6.0);
    state_.angular_velocity += (k1.angular_acceleration + (k2.angular_acceleration + k3.angular_acceleration) * 2.0 +
                                k4.angular_acceleration) *
                               (dt / 6.0);
    state_.time += dt;

    apply_contact_constraint();

    // Detached stages: gravity-only free-flyers, clamped to the surface.
    for (DetachedStage& stage : detached_) {
        if (stage.contact == ContactState::Impacted) {
            continue;
        }
        const Vec2d gravity = model::gravity_acceleration(config_.body, stage.state.position);
        stage.state.velocity += gravity * dt;
        stage.state.position += stage.state.velocity * dt;
        stage.state.time += dt;
        const f64 r = stage.state.position.length();
        if (r <= config_.body.radius && r > 0.0) {
            stage.state.position = stage.state.position * (config_.body.radius / r);
            stage.state.velocity = Vec2d{};
            stage.contact = ContactState::Impacted;
        }
    }
}

void VehicleFlight::advance(const f64 frame_dt) {
    if (!std::isfinite(frame_dt) || frame_dt <= 0.0) {
        return;
    }
    accumulator_ += frame_dt;
    while (accumulator_ >= kFixedDt) {
        tick(kFixedDt);
        accumulator_ -= kFixedDt;
    }
}

}  // namespace aegis::physics
