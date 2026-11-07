// Aegis2D — game session implementation (Milestone 9).
#include "game_session.hpp"

#include <algorithm>
#include <cmath>

namespace aegis::game {

namespace {
constexpr core::f64 kSnapMaxDistance = 6.0;          ///< wu — ghost appears within this of the cursor
constexpr core::f64 kEarthMu = 3.986004418e14;       ///< m^3/s^2
constexpr core::f64 kEarthRadius = 6.371e6;          ///< m
constexpr core::f64 kStandardGravity = 9.80665;

const parts::PartTemplate* template_or(const parts::TemplateId id) noexcept {
    return parts::find_template(id);
}

}  // namespace

GameSession::GameSession() {
    reset_demo();
}

void GameSession::bump_build_version() noexcept {
    ++build_version_;
    preview_dirty_ = true;
}

void GameSession::set_toast(const char* message) noexcept {
    toast_ = message;
    toast_time_ = flight_time_;
}

void GameSession::select_template(const parts::TemplateId id) noexcept {
    if (template_or(id) == nullptr || id == selected_) {
        return;
    }
    selected_ = id;
    bump_build_version();
}

void GameSession::set_cursor_world(const core::f32 world_x, const core::f32 world_y) noexcept {
    if (world_x != cursor_x_ || world_y != cursor_y_) {
        cursor_x_ = world_x;
        cursor_y_ = world_y;
        preview_dirty_ = true;
    }
}

const HoverPreview& GameSession::hover_preview() noexcept {
    if (preview_dirty_) {
        recompute_preview();
    }
    return preview_;
}

void GameSession::recompute_preview() noexcept {
    preview_ = HoverPreview{};
    preview_dirty_ = false;
    const parts::PartTemplate* child = template_or(selected_);
    if (child == nullptr) {
        return;
    }

    // Collect every candidate, then pick by validation order: the nearest
    // geometrically-legal placement that actually survives snap() checks
    // (overlap, grid topology). A nearer-but-occupied slot must never shadow
    // a valid one. Deterministic order (distance, then fixed indices).
    struct Candidate {
        core::f64 d2 = 0.0;
        core::u32 parent = parts::kInvalidPart;
        parts::NodeId parent_node = 0;
        parts::NodeId child_node = 0;
        core::u8 child_rot = 0;
        core::f32 center_x = 0.0f, center_y = 0.0f;
        core::f32 node_x = 0.0f, node_y = 0.0f;
        core::f32 width = 1.0f, height = 1.0f;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(256);

    const auto& part_slots = build_.parts();
    for (core::u32 pi = 0; pi < static_cast<core::u32>(part_slots.size()); ++pi) {
        const parts::Part* part = build_.part(pi);
        if (part == nullptr) {
            continue;
        }
        const parts::PartTemplate* parent = template_or(part->tmpl);
        if (parent == nullptr) {
            continue;
        }
        for (const parts::NodeDef& pnode : parent->nodes) {
            const auto [p_local, p_normal] = parts::transform_node(pnode, parent->w, parent->h, part->rot);
            const core::f32 p_wx = static_cast<core::f32>(part->gx) + p_local[0];
            const core::f32 p_wy = static_cast<core::f32>(part->gy) + p_local[1];
            for (core::u8 child_rot = 0; child_rot < 4; ++child_rot) {
                for (const parts::NodeDef& cnode : child->nodes) {
                    if (!parts::nodes_compatible(pnode.type, cnode.type)) {
                        continue;
                    }
                    const auto [c_local, c_normal] = parts::transform_node(cnode, child->w, child->h, child_rot);
                    const core::f32 dot = p_normal[0] * c_normal[0] + p_normal[1] * c_normal[1];
                    if (dot > -0.99f) {
                        continue;
                    }
                    const core::f32 ox = p_wx - c_local[0];
                    const core::f32 oy = p_wy - c_local[1];
                    const core::f32 rx = std::round(ox);
                    const core::f32 ry = std::round(oy);
                    if (std::fabs(ox - rx) > 1e-3f || std::fabs(oy - ry) > 1e-3f) {
                        continue;
                    }
                    const auto [child_w, child_h] = parts::rotated_dims(child->w, child->h, child_rot);
                    const core::f32 cx = rx + static_cast<core::f32>(child_w) * 0.5f;
                    const core::f32 cy = ry + static_cast<core::f32>(child_h) * 0.5f;
                    const core::f64 dx = static_cast<core::f64>(cx) - static_cast<core::f64>(cursor_x_);
                    const core::f64 dy = static_cast<core::f64>(cy) - static_cast<core::f64>(cursor_y_);
                    Candidate candidate{};
                    candidate.d2 = dx * dx + dy * dy;
                    candidate.parent = pi;
                    candidate.parent_node = pnode.id;
                    candidate.child_node = cnode.id;
                    candidate.child_rot = child_rot;
                    candidate.center_x = cx;
                    candidate.center_y = cy;
                    candidate.node_x = p_wx;
                    candidate.node_y = p_wy;
                    candidate.width = static_cast<core::f32>(child_w);
                    candidate.height = static_cast<core::f32>(child_h);
                    candidates.push_back(candidate);
                }
            }
        }
    }

    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.d2 != b.d2) {
            return a.d2 < b.d2;
        }
        if (a.parent != b.parent) {
            return a.parent < b.parent;
        }
        if (a.child_rot != b.child_rot) {
            return a.child_rot < b.child_rot;
        }
        return a.child_node < b.child_node;
    });

    const auto to_preview = [](const Candidate& candidate, bool valid) {
        HoverPreview out{};
        out.valid = valid;
        out.origin_x = candidate.center_x;
        out.origin_y = candidate.center_y;
        out.width = candidate.width;
        out.height = candidate.height;
        out.rot = candidate.child_rot;
        out.node_x = candidate.node_x;
        out.node_y = candidate.node_y;
        out.has_target = true;
        out.parent_index = candidate.parent;
        out.parent_node = candidate.parent_node;
        out.child_node = candidate.child_node;
        out.child_rot = candidate.child_rot;
        return out;
    };

    constexpr core::f64 kMaxDist2 = static_cast<core::f64>(6.0 * 6.0);
    for (const Candidate& candidate : candidates) {
        if (candidate.d2 > kMaxDist2) {
            break;  // sorted — nothing closer remains
        }
        scratch_ = build_;
        const core::Result<parts::PartIndex> placed = scratch_.snap(candidate.parent, candidate.parent_node,
                                                                    selected_, candidate.child_node,
                                                                    candidate.child_rot);
        if (placed.has_value()) {
            const auto center = scratch_.part_center_wu(placed.value());
            HoverPreview valid = to_preview(candidate, true);
            valid.origin_x = center[0];  // truth from the assembly itself
            valid.origin_y = center[1];
            preview_ = valid;
            return;
        }
    }

    if (candidates.empty()) {
        // Free-floating ghost at the cursor — no anchor exists at all.
        const auto [child_w, child_h] = parts::rotated_dims(child->w, child->h, 0);
        preview_.valid = false;
        preview_.origin_x = cursor_x_;
        preview_.origin_y = cursor_y_;
        preview_.width = static_cast<core::f32>(child_w);
        preview_.height = static_cast<core::f32>(child_h);
        return;
    }
    // Nearest candidate(s) all failed validation: show it as a red ghost.
    preview_ = to_preview(candidates.front(), false);
}

bool GameSession::place_selected() noexcept {
    const HoverPreview preview = hover_preview();  // by value — snap() mutates state
    if (!preview.valid || preview.parent_index == parts::kInvalidPart) {
        set_toast("NO SNAP HERE");
        return false;
    }
    const core::Result<parts::PartIndex> placed =
        build_.snap(preview.parent_index, preview.parent_node, selected_, preview.child_node, preview.child_rot);
    if (!placed.has_value()) {
        set_toast("CANNOT PLACE PART");
        return false;
    }
    bump_build_version();

    if (symmetry_ == Symmetry::MirrorVertical) {
        // Mirror twin across the vertical axis through the command pod (the
        // build centreline, matching the dashed orange VAB axis). Using the
        // pod keeps the axis fixed while asymmetric parts are attached.
        core::f32 axis_wu = 0.5f;
        for (core::u32 pi = 0; pi < static_cast<core::u32>(build_.parts().size()); ++pi) {
            const parts::Part* pod_part = build_.part(pi);
            const parts::PartTemplate* pod_tmpl = pod_part ? template_or(pod_part->tmpl) : nullptr;
            if (pod_tmpl != nullptr && pod_tmpl->category == parts::PartCategory::CommandPod) {
                axis_wu = build_.part_center_wu(pi)[0];
                break;
            }
        }
        const core::Result<parts::PartIndex> twin = build_.mirror_snap(
            preview.parent_index, preview.parent_node, selected_, preview.child_node, preview.child_rot,
            parts::Axis::Vertical, axis_wu);
        if (!twin.has_value()) {
            set_toast("SYMMETRY: NO MATCHING NODE");
        }
        bump_build_version();
    }
    return true;
}

bool GameSession::remove_at_cursor() noexcept {
    // Smallest footprint under the cursor wins; lower index breaks ties.
    const auto& part_slots = build_.parts();
    core::u32 best_index = parts::kInvalidPart;
    core::u64 best_area = ~core::u64{0};
    for (core::u32 pi = 0; pi < static_cast<core::u32>(part_slots.size()); ++pi) {
        const parts::Part* part = build_.part(pi);
        if (part == nullptr) {
            continue;
        }
        const parts::PartTemplate* tmpl = template_or(part->tmpl);
        if (tmpl == nullptr) {
            continue;
        }
        const auto [w, h] = parts::rotated_dims(tmpl->w, tmpl->h, part->rot);
        const auto center = build_.part_center_wu(pi);
        const core::f32 half_w = static_cast<core::f32>(w) * 0.5f;
        const core::f32 half_h = static_cast<core::f32>(h) * 0.5f;
        if (cursor_x_ >= center[0] - half_w && cursor_x_ <= center[0] + half_w &&
            cursor_y_ >= center[1] - half_h && cursor_y_ <= center[1] + half_h) {
            const core::u64 area = static_cast<core::u64>(w) * static_cast<core::u64>(h);
            if (area < best_area || (area == best_area && pi < best_index)) {
                best_area = area;
                best_index = pi;
            }
        }
    }
    if (best_index == parts::kInvalidPart) {
        return false;
    }
    const core::Result<core::u32> removed = build_.remove(best_index);
    if (!removed.has_value()) {
        set_toast("CANNOT REMOVE");
        return false;
    }
    (void)staging_.unassign(best_index);
    bump_build_version();
    return true;
}

void GameSession::clear_assembly() noexcept {
    build_.clear();
    staging_.clear_all();
    bump_build_version();
}

void GameSession::load_preset(const core::u32 index) noexcept {
    if (screen_ == GameScreen::Flight) {
        set_toast("RETURN TO VAB TO SWITCH CRAFT");
        return;
    }
    preset_index_ = index % kPresetCount;
    flight_.reset();
    flight_time_ = 0.0;
    map_overlay_ = false;
    build_ = make_preset_rocket(preset_index_);
    staging_.clear_all();
    (void)staging_.auto_assign(build_);
    preview_dirty_ = true;
    bump_build_version();
    const std::string name = std::string("CRAFT: ") + preset_info(preset_index_).name;
    set_toast(name.c_str());
}

void GameSession::reset_demo() noexcept {
    build_ = make_preset_rocket(preset_index_);
    staging_.clear_all();
    (void)staging_.auto_assign(build_);
    bump_build_version();
}

void GameSession::auto_stage() noexcept {
    (void)staging_.auto_assign(build_);
    bump_build_version();
    set_toast("STAGING AUTO-ASSIGNED");
}

core::f64 GameSession::build_mass_kg() const noexcept {
    return static_cast<core::f64>(build_.mass_properties().total_mass);
}

core::u32 GameSession::part_count() const noexcept {
    return build_.alive_count();
}

core::f64 GameSession::delta_v_estimate() const noexcept {
    const parts::MassProperties mp = build_.mass_properties();
    const core::f64 total = static_cast<core::f64>(mp.total_mass);
    const core::f64 dry = static_cast<core::f64>(mp.dry_mass);
    if (dry <= 0.0 || total <= dry) {
        return 0.0;
    }
    // Effective (mass-flow weighted) Isp over the staged engines.
    core::f64 thrust_sum = 0.0, weighted_isp = 0.0;
    for (const parts::Part& part : build_.parts()) {
        if (part.tmpl == parts::kInvalidTemplate) {
            continue;
        }
        const parts::PartTemplate* tmpl = template_or(part.tmpl);
        if (tmpl == nullptr || !tmpl->is_engine()) {
            continue;
        }
        const core::f64 thrust = static_cast<core::f64>(tmpl->engine.thrust);
        thrust_sum += thrust;
        weighted_isp += thrust * static_cast<core::f64>(tmpl->engine.isp);
    }
    if (thrust_sum <= 0.0) {
        return 0.0;
    }
    const core::f64 isp = weighted_isp / thrust_sum;
    return isp * kStandardGravity * std::log(total / dry);
}

std::string GameSession::launch_blocker() const noexcept {
    bool has_pod = false;
    for (const parts::Part& part : build_.parts()) {
        if (part.tmpl == parts::kInvalidTemplate) {
            continue;
        }
        const parts::PartTemplate* tmpl = template_or(part.tmpl);
        if (tmpl != nullptr && tmpl->category == parts::PartCategory::CommandPod) {
            has_pod = true;
            break;
        }
    }
    if (!has_pod) {
        return "NEED A COMMAND POD";
    }
    if (!build_.validate().empty()) {
        return "AIRFRAME HAS ERRORS";
    }
    bool has_engine = false;
    for (const parts::Part& part : build_.parts()) {
        if (part.tmpl == parts::kInvalidTemplate) {
            continue;
        }
        const parts::PartTemplate* tmpl = template_or(part.tmpl);
        if (tmpl != nullptr && tmpl->is_engine()) {
            has_engine = true;
            break;
        }
    }
    if (!has_engine) {
        return "NEED AN ENGINE";
    }
    return {};
}

bool GameSession::launch() noexcept {
    const std::string blocker = launch_blocker();
    if (!blocker.empty()) {
        set_toast(blocker.c_str());
        return false;
    }
    physics::VehicleConfig config;  // Earth-like body (SI defaults)
    config.aero.drag_coefficient = 0.35;
    config.aero.reference_area = 1.0;
    config.aero.weathercock_gain = 8.0;
    config.aero.weathercock_arm = 2.0;
    config.rotation.control_torque = 1.5e5;   // enough authority for a 7 m stack
    config.rotation.angular_damping = 2.0;

    const parts::MassProperties mp = build_.mass_properties();
    const core::f64 com_y = static_cast<core::f64>(mp.center_of_mass[1]);
    const physics::Vec2d position{0.0, config.body.radius + kPadElevation + com_y};

    auto flight = physics::VehicleFlight::create(parts::VehicleAssembly{build_}, parts::StagingList{staging_},
                                                 config, position, physics::Vec2d{}, 1.5707963267948966);
    if (!flight.has_value()) {
        set_toast("LAUNCH REJECTED");
        return false;
    }
    flight_.emplace(std::move(flight).take());
    flight_time_ = 0.0;
    map_overlay_ = false;
    screen_ = GameScreen::Flight;
    flight_->set_throttle(1.0);  // full thrust at liftoff (SFS convention)
    activate_stage();            // fires stage 0 (liftoff ignititions)
    set_toast("LAUNCH!");
    return true;
}

void GameSession::abort_to_vab() noexcept {
    if (screen_ == GameScreen::Flight) {
        flight_.reset();
        screen_ = GameScreen::Build;
        map_overlay_ = false;
        set_toast("ABORTED TO VAB");
    }
}

const physics::VehicleFlight* GameSession::flight() const noexcept {
    return flight_.has_value() ? &*flight_ : nullptr;
}

void GameSession::set_throttle(const core::f64 throttle) noexcept {
    if (flight_) {
        flight_->set_throttle(throttle);
    }
}

void GameSession::adjust_throttle(const core::f64 delta) noexcept {
    if (flight_) {
        flight_->set_throttle(flight_->throttle() + delta);
    }
}

core::f64 GameSession::throttle() const noexcept {
    return flight_ ? flight_->throttle() : 0.0;
}

void GameSession::set_pitch_input(const core::f64 input) noexcept {
    if (flight_) {
        flight_->set_pitch_input(std::clamp(input, -1.0, 1.0));
    }
}

void GameSession::activate_stage() noexcept {
    if (!flight_) {
        return;
    }
    const core::Result<void> result = flight_->activate_next_stage();
    set_toast(result.has_value() ? "STAGE FIRED" : "NO STAGES LEFT");
}

void GameSession::update(const core::f64 dt_seconds) noexcept {
    if (!flight_) {
        return;
    }
    flight_->advance(dt_seconds);
    flight_time_ += dt_seconds;
    std::vector<physics::FlightEvent> events;
    flight_->take_events(events);
    for (const physics::FlightEvent event : events) {
        switch (event) {
            case physics::FlightEvent::StageSeparated: set_toast("STAGE SEPARATED"); break;
            case physics::FlightEvent::FairingJettisoned: set_toast("FAIRING JETTISONED"); break;
            case physics::FlightEvent::EngineIgnited: set_toast("ENGINE IGNITED"); break;
            case physics::FlightEvent::EngineFlameout: set_toast("FLAMEOUT"); break;
            case physics::FlightEvent::Touchdown: set_toast("TOUCHDOWN"); break;
            case physics::FlightEvent::Impact: set_toast("IMPACT"); break;
        }
    }
}

core::f64 GameSession::altitude_m() const noexcept {
    if (!flight_) {
        return 0.0;
    }
    const physics::FlightState& state = flight_->state();
    return std::max(0.0, state.position.length() - kEarthRadius);
}

core::f64 GameSession::speed_m_s() const noexcept {
    return flight_ ? flight_->state().velocity.length() : 0.0;
}

aegis::orbit::Elements GameSession::orbital_elements() const noexcept {
    aegis::orbit::Elements elements;
    if (!flight_) {
        elements.gravitational_parameter = kEarthMu;
        return elements;
    }
    const physics::FlightState& state = flight_->state();
    return aegis::orbit::elements_from_state({state.position.x, state.position.y},
                                             {state.velocity.x, state.velocity.y},
                                             kEarthMu, state.time);
}

core::f64 GameSession::apoapsis_m() const noexcept {
    if (!flight_) {
        return -1.0;
    }
    const physics::FlightState& state = flight_->state();
    if (state.velocity.length() < 1.0) {
        return altitude_m();
    }
    const aegis::orbit::Elements elements = orbital_elements();
    if (elements.eccentricity >= 1.0) {
        return -1.0;  // unbound
    }
    return aegis::orbit::apoapsis_radius(elements) - kEarthRadius;
}

core::f64 GameSession::periapsis_m() const noexcept {
    if (!flight_) {
        return -1.0;
    }
    const physics::FlightState& state = flight_->state();
    if (state.velocity.length() < 1.0) {
        return altitude_m();
    }
    const aegis::orbit::Elements elements = orbital_elements();
    if (elements.eccentricity >= 1.0) {
        return -1.0;
    }
    return aegis::orbit::periapsis_radius(elements) - kEarthRadius;
}

core::f64 GameSession::propellant_fraction() const noexcept {
    const parts::VehicleAssembly* assembly = &build_;
    if (flight_) {
        assembly = &flight_->assembly();
    }
    core::f64 current = 0.0, capacity = 0.0;
    for (const parts::Part& part : assembly->parts()) {
        if (part.tmpl == parts::kInvalidTemplate) {
            continue;
        }
        const parts::PartTemplate* tmpl = template_or(part.tmpl);
        if (tmpl == nullptr || !tmpl->is_tank()) {
            continue;
        }
        capacity += static_cast<core::f64>(tmpl->tank.propellant);
        current += static_cast<core::f64>(part.propellant);
    }
    return capacity > 0.0 ? current / capacity : 1.0;
}

physics::ContactState GameSession::contact() const noexcept {
    return flight_ ? flight_->contact() : physics::ContactState::Grounded;
}

bool GameSession::impacted() const noexcept {
    return flight_ ? flight_->finished() : false;
}

}  // namespace aegis::game
