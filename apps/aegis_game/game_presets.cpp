// Aegis2D — built-in craft presets (see game_presets.hpp).
#include "game_presets.hpp"

#include <aegis/core/result.hpp>

using namespace aegis;
namespace core = aegis::core;
using aegis::parts::NodeId;
using aegis::parts::NodeType;
using aegis::parts::PartIndex;
using aegis::parts::PartTemplate;
using aegis::parts::TemplateId;
using aegis::parts::VehicleAssembly;

namespace {

constexpr TemplateId kPod = 0;
constexpr TemplateId kLong2 = 1;
constexpr TemplateId kShort1 = 3;
constexpr TemplateId kSparrow = 4;
constexpr TemplateId kKestrel = 6;
constexpr TemplateId kDecoupler = 7;
constexpr TemplateId kRadialSeparator = 9;

/// Stack `child` on the first Top node of `parent` with rotation 0.
core::Result<PartIndex> stack(VehicleAssembly& assembly, const PartIndex parent,
                              const TemplateId child) noexcept {
    const PartTemplate* parent_tmpl = assembly.template_of(parent);
    const PartTemplate* child_tmpl = parts::find_template(child);
    if (parent_tmpl == nullptr || child_tmpl == nullptr) {
        return core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "unknown template");
    }
    const parts::NodeDef* p_top = nullptr;
    const parts::NodeDef* c_bottom = nullptr;
    for (const parts::NodeDef& node : parent_tmpl->nodes) {
        if (node.type == NodeType::Top) {
            p_top = &node;
            break;
        }
    }
    for (const parts::NodeDef& node : child_tmpl->nodes) {
        if (node.type == NodeType::Bottom) {
            c_bottom = &node;
            break;
        }
    }
    if (p_top == nullptr || c_bottom == nullptr) {
        return core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "missing stack node");
    }
    return assembly.snap(parent, p_top->id, child, c_bottom->id, 0);
}

/// Attach a radial separator + (tank, engine) booster to the LEFT (side == -1)
/// or RIGHT (side == +1) lateral node of `parent`. Caller guarantees the grid
/// parity works out (parent lateral local y must be half-integer).
bool snap_booster(VehicleAssembly& assembly, const PartIndex parent, const core::i32 side) noexcept {
    const PartTemplate* parent_tmpl = assembly.template_of(parent);
    if (parent_tmpl == nullptr) {
        return false;
    }
    // Parent lateral node on the requested side.
    const parts::NodeDef* p_lateral = nullptr;
    for (const parts::NodeDef& node : parent_tmpl->nodes) {
        if (node.type == NodeType::Lateral && ((side < 0) ? (node.nx < 0.0f) : (node.nx > 0.0f))) {
            p_lateral = &node;
            break;
        }
    }
    if (p_lateral == nullptr) {
        return false;
    }
    // Separator's facing lateral node (opposite normal to the parent's).
    const PartTemplate* sep_tmpl = parts::find_template(kRadialSeparator);
    const parts::NodeDef sep_facing = side < 0 ? // parent faces left, separator's right side faces the parent
        sep_tmpl->nodes[1] : sep_tmpl->nodes[0];
    const core::Result<PartIndex> separator =
        assembly.snap(parent, p_lateral->id, kRadialSeparator, sep_facing.id, 0);
    if (!separator.has_value()) {
        return false;
    }
    // Booster tank: its facing lateral node toward the separator.
    const PartTemplate* tank_tmpl = parts::find_template(kShort1);
    const parts::NodeDef tank_facing = side < 0 ? tank_tmpl->nodes[3] : tank_tmpl->nodes[2];
    const parts::NodeDef sep_outward = side < 0 ? sep_tmpl->nodes[0] : sep_tmpl->nodes[1];
    const core::Result<PartIndex> tank =
        assembly.snap(separator.value(), sep_outward.id, kShort1, tank_facing.id, 0);
    if (!tank.has_value()) {
        return false;
    }
    // Booster engine below the booster tank.
    const core::Result<PartIndex> engine = stack(assembly, tank.value(), kSparrow);
    return engine.has_value();
}

VehicleAssembly build_hopper() noexcept {  // SPARROW HOPPER — the demo stack
    VehicleAssembly assembly;
    const core::Result<PartIndex> root = assembly.add_root(kSparrow, 0, 0, 0);
    if (!root.has_value()) {
        return assembly;
    }
    const core::Result<PartIndex> tank = stack(assembly, root.value(), kLong2);
    const core::Result<PartIndex> ring = tank.has_value() ? stack(assembly, tank.value(), kDecoupler)
                                                          : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "tank");
    const core::Result<PartIndex> upper = ring.has_value() ? stack(assembly, ring.value(), kSparrow)
                                                           : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "ring");
    const core::Result<PartIndex> upper_tank = upper.has_value() ? stack(assembly, upper.value(), kShort1)
                                                                 : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "upper");
    if (upper_tank.has_value()) {
        (void)stack(assembly, upper_tank.value(), kPod);
    }
    return assembly;
}

VehicleAssembly build_orbiter() noexcept {  // TWIN-TANK ORBITER — two Long-2s, Kestrel upper
    VehicleAssembly assembly;
    const core::Result<PartIndex> root = assembly.add_root(kSparrow, 0, 0, 0);
    if (!root.has_value()) {
        return assembly;
    }
    const core::Result<PartIndex> tank_a = stack(assembly, root.value(), kLong2);
    const core::Result<PartIndex> tank_b = tank_a.has_value()
                                               ? stack(assembly, tank_a.value(), kLong2)
                                               : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "tank a");
    const core::Result<PartIndex> ring = tank_b.has_value() ? stack(assembly, tank_b.value(), kDecoupler)
                                                            : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "tank b");
    const core::Result<PartIndex> upper = ring.has_value() ? stack(assembly, ring.value(), kKestrel)
                                                           : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "ring");
    const core::Result<PartIndex> upper_tank = upper.has_value() ? stack(assembly, upper.value(), kShort1)
                                                                 : core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "upper");
    if (upper_tank.has_value()) {
        (void)stack(assembly, upper_tank.value(), kPod);
    }
    return assembly;
}

VehicleAssembly build_triple() noexcept {  // RADIAL TRIPLE — Sparrow + two radial boosters
    VehicleAssembly assembly;
    const core::Result<PartIndex> root = assembly.add_root(kSparrow, 0, 0, 0);
    if (!root.has_value()) {
        return assembly;
    }
    const core::Result<PartIndex> core_tank = stack(assembly, root.value(), kShort1);
    if (!core_tank.has_value()) {
        return assembly;
    }
    const core::Result<PartIndex> ring = stack(assembly, core_tank.value(), kDecoupler);
    if (ring.has_value()) {
        const core::Result<PartIndex> upper = stack(assembly, ring.value(), kSparrow);
        if (upper.has_value()) {
            const core::Result<PartIndex> upper_tank = stack(assembly, upper.value(), kShort1);
            if (upper_tank.has_value()) {
                (void)stack(assembly, upper_tank.value(), kPod);
            }
        }
    }
    // Boosters ride the core tank's lateral hardpoints (local y = 0.5 → half-integer grid).
    (void)snap_booster(assembly, core_tank.value(), -1);
    (void)snap_booster(assembly, core_tank.value(), +1);
    return assembly;
}

constexpr aegis::game::CraftPresetInfo kPresetInfos[aegis::game::kPresetCount] = {
    {"SPARROW HOPPER", "6 PARTS  |  Sparrow two-stage hopper — the VAB default", 6, 3675.0},
    {"TWIN-TANK ORBITER", "7 PARTS  |  Sparrow + 2x Long-2, Kestrel upper — max dV", 7, 5320.0},
    {"RADIAL TRIPLE", "12 PARTS  |  3 Sparrows from the pad, separators clear at STG 1", 12, 4191.0},
};

}  // namespace

namespace aegis::game {

const CraftPresetInfo& preset_info(const core::u32 index) noexcept {
    return kPresetInfos[index % kPresetCount];
}

parts::VehicleAssembly make_preset_rocket(const core::u32 index) noexcept {
    switch (index % kPresetCount) {
        case 1:
            return build_orbiter();
        case 2:
            return build_triple();
        case 0:
        default:
            return build_hopper();
    }
}

}  // namespace aegis::game
