// Aegis2D — M7 parts module tests: registry, rotation/snap math, building,
// removal, splitting, staging, mass properties.
#include <aegis/parts/part_template.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>

#include <algorithm>
#include <cmath>
#include <set>

#include "aegis_test.hpp"


// Expectation helper (reports, then the harness continues).
#define EXPECT_OK_PARTS(res)                                                                  \
    do {                                                                                       \
        if (!(res).has_value()) {                                                              \
            ::aegis::test::report_failure(__FILE__, __LINE__,                                  \
                                          std::string("unexpected failure: ") +                 \
                                          (res).error().to_string());                           \
        }                                                                                      \
    } while (0)

namespace aegis::parts {
namespace {

constexpr TemplateId kPod = 0;
constexpr TemplateId kLongTank2 = 1;
constexpr TemplateId kLongTank4 = 2;
constexpr TemplateId kShortTank = 3;
constexpr TemplateId kSparrow = 4;
constexpr TemplateId kKestrel = 6;
constexpr TemplateId kDecoupler = 7;
constexpr TemplateId kRadialSeparator = 9;
constexpr TemplateId kNoseFairing = 11;
constexpr TemplateId kStrut = 12;

bool near(const f32 a, const f32 b, const f32 eps = 1e-4f) noexcept {
    return std::fabs(a - b) <= eps;
}

// Build the canonical three-stage demo rocket (mirrors tools/aegis_assembly_check).
struct DemoRocket {
    VehicleAssembly asmbl;
    PartIndex liftoff, sep1, kestrel, tank2, sep2, tank3, pod, fairing;
    PartIndex sep_left, booster_left_tank, booster_left_engine;
    PartIndex sep_right, booster_right_tank, booster_right_engine;
};

DemoRocket build_demo_rocket() {
    DemoRocket r;
    auto add = [&r](const TemplateId tmpl, const i16 gx, const i16 gy, const u8 rot) {
        core::Result<PartIndex> res = r.asmbl.add_root(tmpl, gx, gy, rot);
        EXPECT_OK_PARTS(res);
        return res.value();
    };
    auto snap = [&r](const PartIndex parent, const NodeId pnode, const TemplateId child,
                     const NodeId cnode, const u8 rot) {
        core::Result<PartIndex> res = r.asmbl.snap(parent, pnode, child, cnode, rot);
        EXPECT_OK_PARTS(res);
        return res.value();
    };
    r.liftoff = add(kSparrow, 0, 0, 0);
    r.sep1 = snap(r.liftoff, 0, kDecoupler, 1, 0);
    r.kestrel = snap(r.sep1, 0, kKestrel, 1, 0);
    r.tank2 = snap(r.kestrel, 0, kShortTank, 1, 0);
    r.sep2 = snap(r.tank2, 0, kDecoupler, 1, 0);
    r.tank3 = snap(r.sep2, 0, kShortTank, 1, 0);
    r.pod = snap(r.tank3, 0, kPod, 0, 0);
    r.fairing = snap(r.pod, 3, kNoseFairing, 0, 0);
    r.sep_left = snap(r.tank2, 2, kRadialSeparator, 1, 0);
    r.booster_left_tank = snap(r.sep_left, 0, kShortTank, 3, 0);
    r.booster_left_engine = snap(r.booster_left_tank, 1, kSparrow, 0, 0);
    core::Result<PartIndex> sep_right =
        r.asmbl.mirror_snap(r.tank2, 2, kRadialSeparator, 1, 0, Axis::Vertical, 0.5f);
    EXPECT_OK_PARTS(sep_right);
    r.sep_right = sep_right.value();
    r.booster_right_tank = snap(r.sep_right, 0, kShortTank, 2, 0);
    r.booster_right_engine = snap(r.booster_right_tank, 1, kSparrow, 0, 0);
    return r;
}

}  // namespace
}  // namespace aegis::parts

namespace aegis::parts {
namespace {

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "built-in registry loads with contiguous ids") {
    const auto& registry = templates();
    AEGIS_CHECK_EQ(registry.size(), kTemplateCount);
    for (std::size_t i = 0; i < registry.size(); ++i) {
        AEGIS_CHECK_EQ(registry[i].id, TemplateId(i));
        AEGIS_CHECK(!std::string_view(registry[i].name).empty());
        AEGIS_CHECK(registry[i].w >= 1 && registry[i].h >= 1);
        AEGIS_CHECK(registry[i].dry_mass > 0.0f);
        AEGIS_CHECK(!registry[i].nodes.empty());
    }
}

AEGIS_TEST(parts, "find_template rejects out-of-range ids") {
    AEGIS_CHECK(find_template(0) != nullptr);
    AEGIS_CHECK(find_template(kTemplateCount - 1) != nullptr);
    AEGIS_CHECK(find_template(kTemplateCount) == nullptr);
    AEGIS_CHECK(find_template(0xFFFE) == nullptr);
}

AEGIS_TEST(parts, "every node normal is a unit vector") {
    for (const PartTemplate& tmpl : templates()) {
        for (const NodeDef& node : tmpl.nodes) {
            const f32 len = std::sqrt(node.nx * node.nx + node.ny * node.ny);
            AEGIS_CHECK(near(len, 1.0f, 1e-5f));
        }
    }
}

AEGIS_TEST(parts, "category-specific payloads are set") {
    for (const PartTemplate& tmpl : templates()) {
        if (tmpl.is_tank()) {
            AEGIS_CHECK(tmpl.tank.propellant > 0.0f);
        }
        if (tmpl.is_engine()) {
            AEGIS_CHECK(tmpl.engine.thrust > 0.0f);
            AEGIS_CHECK(tmpl.engine.isp > 0.0f);
        }
        if (tmpl.is_decoupler()) {
            AEGIS_CHECK(tmpl.decoupler.separation_delta_v > 0.0f);
        }
    }
}

AEGIS_TEST(parts, "engines carry stack nodes and decouplers carry attachment pairs") {
    for (const PartTemplate& tmpl : templates()) {
        if (tmpl.is_engine()) {
            bool top = false, bottom = false;
            for (const NodeDef& node : tmpl.nodes) {
                top = top || node.type == NodeType::Top;
                bottom = bottom || node.type == NodeType::Bottom;
            }
            AEGIS_CHECK(top && bottom);
        }
        if (tmpl.is_decoupler()) {
            AEGIS_CHECK(tmpl.nodes.size() >= 2);
        }
    }
}

// ---------------------------------------------------------------------------
// Rotation + footprint
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "rotation encoding packs steps and the flip bit") {
    AEGIS_CHECK(rot_of(0) == 0 && !flipped(0));
    AEGIS_CHECK(rot_of(3) == 3 && !flipped(3));
    AEGIS_CHECK(make_rot(2, true) == (2 | kFlipBit));
    AEGIS_CHECK(flipped(make_rot(1, true)));
    AEGIS_CHECK(rot_of(make_rot(2, true)) == 2);
}

AEGIS_TEST(parts, "mirror rotation rules") {
    AEGIS_CHECK(mirror_rot_vertical(make_rot(1, false)) == make_rot(3, true));
    AEGIS_CHECK(mirror_rot_vertical(make_rot(0, false)) == make_rot(0, true));
    AEGIS_CHECK(mirror_rot_vertical(make_rot(2, true)) == make_rot(2, false));
    AEGIS_CHECK(mirror_rot_horizontal(make_rot(0, false)) == make_rot(2, true));
    AEGIS_CHECK(mirror_rot_horizontal(make_rot(1, false)) == make_rot(1, true));
    AEGIS_CHECK(mirror_rot_horizontal(make_rot(3, false)) == make_rot(3, true));
}

AEGIS_TEST(parts, "footprint of a 1x1 at every rotation covers one cell") {
    for (u8 r = 0; r < 4; ++r) {
        const Footprint foot = footprint_of(1, 1, r);
        AEGIS_CHECK_EQ(foot.count, 1);
        AEGIS_CHECK_EQ(foot.w, 1);
        AEGIS_CHECK_EQ(foot.h, 1);
        AEGIS_CHECK_EQ(foot.cells[0].first, 0);
        AEGIS_CHECK_EQ(foot.cells[0].second, 0);
    }
}

AEGIS_TEST(parts, "footprint of a 1x2 at rot 0 is vertical and at rot 1 horizontal") {
    const Footprint a = footprint_of(1, 2, 0);
    AEGIS_CHECK_EQ(a.w, 1);
    AEGIS_CHECK_EQ(a.h, 2);
    const std::set<std::pair<i16, i16>> cells_a{a.cells.begin(), a.cells.begin() + a.count};
    AEGIS_CHECK((cells_a == std::set<std::pair<i16, i16>>{{0, 0}, {0, 1}}));

    const Footprint b = footprint_of(1, 2, 1);
    AEGIS_CHECK_EQ(b.w, 2);
    AEGIS_CHECK_EQ(b.h, 1);
    const std::set<std::pair<i16, i16>> cells_b{b.cells.begin(), b.cells.begin() + b.count};
    AEGIS_CHECK((cells_b == std::set<std::pair<i16, i16>>{{0, 0}, {1, 0}}));
}

AEGIS_TEST(parts, "footprint of a 2x4 rotated covers the full 4x2 rectangle") {
    const Footprint foot = footprint_of(2, 4, 1);
    AEGIS_CHECK_EQ(foot.w, 4);
    AEGIS_CHECK_EQ(foot.h, 2);
    AEGIS_CHECK_EQ(foot.count, 8);
    const std::set<std::pair<i16, i16>> cells{foot.cells.begin(), foot.cells.begin() + foot.count};
    for (i16 y = 0; y < 2; ++y) {
        for (i16 x = 0; x < 4; ++x) {
            AEGIS_CHECK(cells.count({x, y}) == 1);
        }
    }
}

AEGIS_TEST(parts, "flip does not change a square footprint's cells") {
    const Footprint square = footprint_of(1, 1, make_rot(0, true));
    AEGIS_CHECK_EQ(square.count, 1);
    AEGIS_CHECK_EQ(square.cells[0].first, 0);
    AEGIS_CHECK_EQ(square.cells[0].second, 0);

    const Footprint wide = footprint_of(2, 1, make_rot(0, true));
    const std::set<std::pair<i16, i16>> cells{wide.cells.begin(), wide.cells.begin() + wide.count};
    AEGIS_CHECK((cells == std::set<std::pair<i16, i16>>{{0, 0}, {1, 0}}));
}

AEGIS_TEST(parts, "rotated_dims swaps on odd steps") {
    AEGIS_CHECK(rotated_dims(2, 4, 0) == std::make_pair(u8(2), u8(4)));
    AEGIS_CHECK(rotated_dims(2, 4, 1) == std::make_pair(u8(4), u8(2)));
    AEGIS_CHECK(rotated_dims(2, 4, 2) == std::make_pair(u8(2), u8(4)));
    AEGIS_CHECK(rotated_dims(2, 4, 3) == std::make_pair(u8(4), u8(2)));
    AEGIS_CHECK(rotated_dims(2, 4, make_rot(1, true)) == std::make_pair(u8(4), u8(2)));
}

// ---------------------------------------------------------------------------
// Node transforms
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "node transform at rot 0 is the identity") {
    const PartTemplate& pod = *find_template(kPod);
    const auto [pos, normal] = transform_node(pod.nodes[0], pod.w, pod.h, 0);
    AEGIS_CHECK(near(pos[0], 0.5f) && near(pos[1], 0.0f));
    AEGIS_CHECK(near(normal[0], 0.0f) && near(normal[1], -1.0f));
}

AEGIS_TEST(parts, "node normals rotate CCW with the part") {
    const PartTemplate& pod = *find_template(kPod);
    const auto bottom = transform_node(pod.nodes[0], pod.w, pod.h, 1).second;  // (0,-1) -> (1,0)
    AEGIS_CHECK(near(bottom[0], 1.0f) && near(bottom[1], 0.0f));
    const auto top = transform_node(pod.nodes[3], pod.w, pod.h, 1).second;  // (0,1) -> (-1,0)
    AEGIS_CHECK(near(top[0], -1.0f) && near(top[1], 0.0f));
}

AEGIS_TEST(parts, "flip mirrors node positions and normals within the footprint") {
    const PartTemplate& shell = *find_template(kRadialSeparator);
    const auto right = transform_node(shell.nodes[1], shell.w, shell.h, make_rot(0, true));
    AEGIS_CHECK(near(right.first[0], 0.0f) && near(right.first[1], 0.5f));
    AEGIS_CHECK(near(right.second[0], -1.0f) && near(right.second[1], 0.0f));
}

AEGIS_TEST(parts, "node normal flips direction at rot 2") {
    const PartTemplate& pod = *find_template(kPod);
    const auto top = transform_node(pod.nodes[3], pod.w, pod.h, 2).second;
    AEGIS_CHECK(near(top[0], 0.0f) && near(top[1], -1.0f));
}

// ---------------------------------------------------------------------------
// Building + snapping
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "add_root then snap engine under a tank") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> engine = asmbl.snap(tank.value(), 1, kSparrow, 0, 0);
    AEGIS_REQUIRE(engine.has_value());
    AEGIS_CHECK_EQ(asmbl.alive_count(), 2u);
    AEGIS_CHECK(asmbl.part(engine.value())->gy == -1);
}

AEGIS_TEST(parts, "snap rejects an unknown child template") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> bad = asmbl.snap(tank.value(), 1, kInvalidTemplate, 0, 0);
    AEGIS_CHECK(!bad.has_value());
}

AEGIS_TEST(parts, "snap rejects a dead parent part") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    AEGIS_REQUIRE(pod.has_value());
    AEGIS_CHECK(!asmbl.snap(999, 0, kSparrow, 0, 0).has_value());
}

AEGIS_TEST(parts, "snap rejects node type pairs outside the compatible table") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    // Top <-> Top is not a legal pair.
    const core::Result<PartIndex> bad = asmbl.snap(tank.value(), 0, kShortTank, 0, 0);
    AEGIS_CHECK(!bad.has_value());
}

AEGIS_TEST(parts, "snap rejects normals that do not oppose") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    // Bottom (parent, down-facing) against the child's bottom node: same direction.
    const core::Result<PartIndex> bad = asmbl.snap(tank.value(), 1, kShortTank, 1, 0);
    AEGIS_CHECK(!bad.has_value());
}

AEGIS_TEST(parts, "snap rejects a placement that would not align to the grid") {
    VehicleAssembly asmbl;
    // 2x4 tank's lateral nodes sit on integer Y; a 1-cell lateral mate needs half-integers.
    const core::Result<PartIndex> tank = asmbl.add_root(kLongTank4, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> bad = asmbl.snap(tank.value(), 2, kShortTank, 3, 0);
    AEGIS_CHECK(!bad.has_value());
}

AEGIS_TEST(parts, "add_root rejects an overlapping placement") {
    VehicleAssembly asmbl;
    AEGIS_REQUIRE(asmbl.add_root(kPod, 0, 0, 0).has_value());
    const core::Result<PartIndex> dup = asmbl.add_root(kPod, 0, 0, 0);
    AEGIS_CHECK(!dup.has_value());
    AEGIS_CHECK(dup.error().code == core::ErrorCode::AlreadyExists);
}

AEGIS_TEST(parts, "snap detects overlap with an existing part") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    AEGIS_REQUIRE(pod.has_value());
    AEGIS_REQUIRE(asmbl.add_root(kShortTank, 0, -1, 0).has_value());
    // Pod's bottom node wants the cell below the pod — already occupied.
    const core::Result<PartIndex> bad = asmbl.snap(pod.value(), 0, kSparrow, 0, 0);
    AEGIS_CHECK(!bad.has_value());
}

AEGIS_TEST(parts, "snap above a separator works at rotation 0 with engine bottom nodes") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> sep = asmbl.add_root(kDecoupler, 0, 0, 0);
    AEGIS_REQUIRE(sep.has_value());
    const core::Result<PartIndex> engine = asmbl.snap(sep.value(), 0, kKestrel, 1, 0);
    AEGIS_REQUIRE(engine.has_value());
    AEGIS_CHECK_EQ(asmbl.part(engine.value())->gy, 1);
}

AEGIS_TEST(parts, "remove drops the target and every part orphaned by the cut") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> engine = asmbl.snap(tank.value(), 1, kSparrow, 0, 0);
    AEGIS_REQUIRE(engine.has_value());
    const core::Result<PartIndex> pod = asmbl.snap(tank.value(), 0, kPod, 0, 0);
    AEGIS_REQUIRE(pod.has_value());

    const core::Result<u32> removed = asmbl.remove(tank.value());
    AEGIS_REQUIRE(removed.has_value());
    AEGIS_CHECK_EQ(removed.value(), 2u);  // tank + engine die; the orphan cannot stay
    AEGIS_CHECK(!asmbl.is_alive(tank.value()));
    AEGIS_CHECK(!asmbl.is_alive(engine.value()));
    AEGIS_CHECK(asmbl.is_alive(pod.value()));  // pod is its own root, stays
}

AEGIS_TEST(parts, "removing a leaf tail keeps the pod tree intact") {
    DemoRocket r = build_demo_rocket();
    const core::Result<u32> removed = r.asmbl.remove(r.liftoff);
    AEGIS_REQUIRE(removed.has_value());
    AEGIS_CHECK_EQ(removed.value(), 1u);
    AEGIS_CHECK(r.asmbl.validate().empty());
}

// ---------------------------------------------------------------------------
// Symmetry helper
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "mirror_snap places a mirrored counterpart with the flip bit") {
    DemoRocket r = build_demo_rocket();
    const std::array<f32, 2> left = r.asmbl.part_center_wu(r.sep_left);
    const std::array<f32, 2> right = r.asmbl.part_center_wu(r.sep_right);
    AEGIS_CHECK(near(left[0] + right[0], 1.0f));
    AEGIS_CHECK(near(left[1], right[1]));
    AEGIS_CHECK_EQ(r.asmbl.part(r.sep_right)->rot, kFlipBit);
}

AEGIS_TEST(parts, "mirror_snap fails cleanly when no counterpart node exists") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kLongTank4, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> missing =
        asmbl.mirror_snap(tank.value(), 2, kShortTank, 3, 0, Axis::Horizontal, 100.0f);
    AEGIS_CHECK(!missing.has_value());
    AEGIS_CHECK(missing.error().code == core::ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Adjacency (derived, never stored)
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "adjacency reports exactly the mating edge") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> engine = asmbl.snap(tank.value(), 1, kSparrow, 0, 0);
    AEGIS_REQUIRE(engine.has_value());
    const std::vector<std::pair<u32, u32>> edges = asmbl.adjacency();
    AEGIS_REQUIRE(edges.size() == 1);
    AEGIS_CHECK_EQ(edges[0].first, tank.value());
    AEGIS_CHECK_EQ(edges[0].second, engine.value());
}

AEGIS_TEST(parts, "adjacency is sorted and deduplicated on a full rocket") {
    DemoRocket r = build_demo_rocket();
    const std::vector<std::pair<u32, u32>> edges = r.asmbl.adjacency();
    AEGIS_CHECK(std::is_sorted(edges.begin(), edges.end()));
    AEGIS_CHECK(std::adjacent_find(edges.begin(), edges.end()) == edges.end());
    AEGIS_CHECK_EQ(edges.size(), 13u);  // full demo rocket graph
}

AEGIS_TEST(parts, "tombstoned parts disappear from adjacency") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> engine = asmbl.snap(tank.value(), 1, kSparrow, 0, 0);
    AEGIS_REQUIRE(engine.has_value());
    AEGIS_REQUIRE(asmbl.remove(engine.value()).has_value());
    AEGIS_CHECK(asmbl.adjacency().empty());
}

// ---------------------------------------------------------------------------
// Mass properties
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "a lone pod has zero Izz and its own CoM") {
    VehicleAssembly asmbl;
    AEGIS_REQUIRE(asmbl.add_root(kPod, 0, 0, 0).has_value());
    const MassProperties& mp = asmbl.mass_properties();
    AEGIS_CHECK(near(mp.total_mass, 800.0f));
    AEGIS_CHECK(near(mp.center_of_mass[0], 0.5f));
    AEGIS_CHECK(near(mp.center_of_mass[1], 0.5f));
    AEGIS_CHECK(near(mp.inertia_izz, 0.0f));
}

AEGIS_TEST(parts, "two stacked parts aggregate with mass weighting") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    AEGIS_REQUIRE(pod.has_value());
    const core::Result<PartIndex> fairing = asmbl.snap(pod.value(), 3, kNoseFairing, 0, 0);
    AEGIS_REQUIRE(fairing.has_value());
    const MassProperties& mp = asmbl.mass_properties();
    const f32 expected_com = (800.0f * 0.5f + 32.0f * 1.5f) / 832.0f;
    AEGIS_CHECK(near(mp.center_of_mass[1], expected_com, 1e-4f));
    AEGIS_CHECK(mp.inertia_izz > 0.0f);
}

AEGIS_TEST(parts, "CoM follows the analytic two-part solution for a pod and tank") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> tank = asmbl.add_root(kShortTank, 0, 0, 0);
    AEGIS_REQUIRE(tank.has_value());
    const core::Result<PartIndex> pod = asmbl.snap(tank.value(), 0, kPod, 0, 0);
    AEGIS_REQUIRE(pod.has_value());
    const MassProperties& mp = asmbl.mass_properties();
    const f32 tank_mass = templates()[kShortTank].dry_mass + templates()[kShortTank].tank.propellant;
    const f32 expected_com = (tank_mass * 0.5f + 800.0f * 1.5f) / (tank_mass + 800.0f);
    AEGIS_CHECK(near(mp.center_of_mass[1], expected_com, 1e-4f));
}

AEGIS_TEST(parts, "fresh demo rocket aggregates exact registry mass") {
    DemoRocket r = build_demo_rocket();
    const MassProperties& mp = r.asmbl.mass_properties();
    const f32 pod = templates()[kPod].dry_mass;
    const f32 short_tank = templates()[kShortTank].dry_mass;
    const f32 prop = templates()[kShortTank].tank.propellant;
    const f32 expected = pod + short_tank * 4 + prop * 4 + templates()[kSparrow].dry_mass * 3 +
                         templates()[kKestrel].dry_mass + templates()[kDecoupler].dry_mass * 2 +
                         templates()[kRadialSeparator].dry_mass * 2 + templates()[kNoseFairing].dry_mass;
    AEGIS_CHECK(near(mp.total_mass, expected, 1e-3f));
    AEGIS_CHECK(near(mp.propellant_mass, prop * 4, 1e-3f));
}

AEGIS_TEST(parts, "Izz matches the analytic point-mass solution") {
    VehicleAssembly asmbl;
    const core::Result<PartIndex> pod = asmbl.add_root(kPod, 0, 0, 0);
    AEGIS_REQUIRE(pod.has_value());
    const core::Result<PartIndex> fairing = asmbl.snap(pod.value(), 3, kNoseFairing, 0, 0);
    AEGIS_REQUIRE(fairing.has_value());
    const MassProperties& mp = asmbl.mass_properties();
    const f32 total = 832.0f;
    const f32 com = (800.0f * 0.5f + 32.0f * 1.5f) / total;
    const f32 expected_izz = 800.0f * (0.5f - com) * (0.5f - com) + 32.0f * (1.5f - com) * (1.5f - com);
    AEGIS_CHECK(near(mp.inertia_izz, expected_izz, 1e-2f));
}

// ---------------------------------------------------------------------------
// Splitting (decouplers)
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "split_at_decouplers separates the liftoff stage and preserves indices") {
    DemoRocket r = build_demo_rocket();
    const std::vector<VehicleAssembly> components = r.asmbl.split_at_decouplers({r.sep1});
    AEGIS_CHECK_EQ(components.size(), 2u);

    const VehicleAssembly* vehicle = nullptr;
    const VehicleAssembly* booster_side = nullptr;
    for (const VehicleAssembly& c : components) {
        if (c.is_alive(r.pod)) {
            vehicle = &c;
        } else {
            booster_side = &c;
        }
    }
    AEGIS_REQUIRE(vehicle != nullptr && booster_side != nullptr);
    AEGIS_CHECK(booster_side->is_alive(r.liftoff));
    AEGIS_CHECK(booster_side->is_alive(r.sep1));  // carcass stays with the lower stage
    AEGIS_CHECK_EQ(booster_side->alive_count(), 2u);
    AEGIS_CHECK_EQ(vehicle->alive_count(), 12u);
    // Indices are stable: the tank below the pod is still at its original slot.
    AEGIS_CHECK(vehicle->is_alive(r.tank3));
}

AEGIS_TEST(parts, "split_at_decouplers ignores invalid indices gracefully") {
    DemoRocket r = build_demo_rocket();
    const std::vector<VehicleAssembly> components = r.asmbl.split_at_decouplers({0xFFFF});
    AEGIS_CHECK_EQ(components.size(), 1u);
    AEGIS_CHECK_EQ(components[0].alive_count(), 14u);
}

AEGIS_TEST(parts, "split_at_decouplers cuts both separators in one pass") {
    DemoRocket r = build_demo_rocket();
    const std::vector<VehicleAssembly> components =
        r.asmbl.split_at_decouplers({r.sep_left, r.sep_right});
    AEGIS_CHECK_EQ(components.size(), 3u);
    for (const VehicleAssembly& c : components) {
        if (c.is_alive(r.tank2)) {
            continue;  // the core
        }
        AEGIS_CHECK_EQ(c.alive_count(), 2u);  // engine + tank per booster
    }
}

// ---------------------------------------------------------------------------
// Staging
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "assign rejects non-stageable parts and unknown indices") {
    DemoRocket r = build_demo_rocket();
    StagingList staging;
    AEGIS_CHECK(!staging.assign(r.asmbl, r.tank2, 0).has_value());
    AEGIS_CHECK(!staging.assign(r.asmbl, 999, 0).has_value());
}

AEGIS_TEST(parts, "assign, stage_of, is_staged and unassign round trip") {
    DemoRocket r = build_demo_rocket();
    StagingList staging;
    AEGIS_CHECK(staging.assign(r.asmbl, r.liftoff, 2).has_value());
    AEGIS_CHECK(staging.is_staged(r.liftoff));
    AEGIS_CHECK_EQ(staging.stage_of(r.liftoff), 2);
    AEGIS_CHECK(staging.unassign(r.liftoff).has_value());
    AEGIS_CHECK(!staging.is_staged(r.liftoff));
    AEGIS_CHECK_EQ(staging.stage_of(r.liftoff), kUnstaged);
}

AEGIS_TEST(parts, "firing order within a stage is decouplers before engines") {
    DemoRocket r = build_demo_rocket();
    StagingList staging;
    AEGIS_REQUIRE(staging.assign(r.asmbl, r.kestrel, 9).has_value());
    AEGIS_REQUIRE(staging.assign(r.asmbl, r.sep2, 9).has_value());
    core::Result<StageFireResult> result = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(result.has_value());
    AEGIS_REQUIRE(result.value().fired.size() == 2);
    AEGIS_CHECK(result.value().fired[0].action == StageAction::FireDecoupler);
    AEGIS_CHECK(result.value().fired[0].part == r.sep2);
    AEGIS_CHECK(result.value().fired[1].action == StageAction::IgniteEngine);
    AEGIS_CHECK(result.value().fired[1].part == r.kestrel);
}

AEGIS_TEST(parts, "activation walks the whole flight plan and then fails cleanly") {
    DemoRocket r = build_demo_rocket();
    StagingList staging;
    for (const auto& [part, stage] : std::vector<std::pair<PartIndex, u8>>{
             {r.liftoff, 0},   {r.booster_left_engine, 0}, {r.booster_right_engine, 0},
             {r.sep1, 1},      {r.sep_left, 2},            {r.sep_right, 2},
             {r.kestrel, 3},   {r.sep2, 4},                {r.fairing, 5}}) {
        AEGIS_REQUIRE(staging.assign(r.asmbl, part, stage).has_value());
    }
    AEGIS_CHECK_EQ(staging.stage_count(), 6);

    core::Result<StageFireResult> s0 = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(s0.has_value());
    AEGIS_CHECK_EQ(s0.value().fired.size(), 3u);
    AEGIS_CHECK(s0.value().detached.empty());

    core::Result<StageFireResult> s1 = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(s1.has_value());
    AEGIS_CHECK_EQ(s1.value().detached.size(), 1u);

    core::Result<StageFireResult> s2 = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(s2.has_value());
    AEGIS_CHECK_EQ(s2.value().detached.size(), 2u);
    AEGIS_CHECK_EQ(r.asmbl.alive_count(), 8u);

    core::Result<StageFireResult> s3 = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(s3.has_value());
    AEGIS_CHECK_EQ(s3.value().detached.size(), 0u);

    core::Result<StageFireResult> s4 = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(s4.has_value());
    AEGIS_CHECK_EQ(s4.value().detached.size(), 1u);
    AEGIS_CHECK_EQ(r.asmbl.alive_count(), 3u);

    core::Result<StageFireResult> s5 = staging.activate_next_stage(r.asmbl);
    AEGIS_REQUIRE(s5.has_value());
    AEGIS_CHECK(!r.asmbl.is_alive(r.fairing));

    core::Result<StageFireResult> exhausted = staging.activate_next_stage(r.asmbl);
    AEGIS_CHECK(!exhausted.has_value());
}

AEGIS_TEST(parts, "auto_assign puts the liftoff group in stage zero deterministically") {
    DemoRocket r = build_demo_rocket();
    StagingList staging;
    const u8 count = staging.auto_assign(r.asmbl);
    AEGIS_CHECK(count >= 3);
    AEGIS_CHECK_EQ(staging.cursor(), 0);
    AEGIS_CHECK_EQ(staging.stage_of(r.liftoff), 0);
    AEGIS_CHECK(staging.stage_of(r.tank3) == kUnstaged);
    AEGIS_CHECK(staging.stage_of(r.booster_left_engine) != kUnstaged);
    // The booster group shares a stage with the core's upper engine (heuristic groups by depth).
    AEGIS_CHECK(staging.stage_of(r.booster_left_engine) == staging.stage_of(r.kestrel));
}

AEGIS_TEST(parts, "fairing jettison tombstones just the fairing parts") {
    DemoRocket r = build_demo_rocket();
    const core::Result<u32> dropped = r.asmbl.jettison_fairings({r.fairing});
    AEGIS_REQUIRE(dropped.has_value());
    AEGIS_CHECK_EQ(dropped.value(), 1u);
    AEGIS_CHECK(!r.asmbl.is_alive(r.fairing));
    AEGIS_CHECK(r.asmbl.validate().empty());
    const core::Result<u32> bad = r.asmbl.jettison_fairings({r.pod});
    AEGIS_CHECK(!bad.has_value());
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "an empty assembly reports the missing command pod") {
    VehicleAssembly asmbl;
    const std::vector<AssemblyIssue> issues = asmbl.validate();
    AEGIS_REQUIRE(!issues.empty());
    AEGIS_CHECK(issues[0].kind == AssemblyIssue::Kind::NoCommandPod);
}

AEGIS_TEST(parts, "a fully built demo rocket validates clean") {
    DemoRocket r = build_demo_rocket();
    AEGIS_CHECK(r.asmbl.validate().empty());
}

// ---------------------------------------------------------------------------
// Hash helper
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "hash_cell distinguishes negative and transposed coordinates") {
    AEGIS_CHECK(hash_cell(-1, 2) != hash_cell(2, -1));
    AEGIS_CHECK(hash_cell(0, 0) == hash_cell(0, 0));
    AEGIS_CHECK(hash_cell(-1, -1) != hash_cell(1, 1));
    AEGIS_CHECK(hash_cell(0, 1) != hash_cell(1, 0));
}

// ---------------------------------------------------------------------------
// Propellant consumption (M8)
// ---------------------------------------------------------------------------

AEGIS_TEST(parts, "consume_propellant drains a tank exactly") {
    DemoRocket r = build_demo_rocket();
    const core::Result<f32> drained = r.asmbl.consume_propellant(r.tank2, 100.0f);
    AEGIS_REQUIRE(drained.has_value());
    AEGIS_CHECK(near(drained.value(), 100.0f));
    AEGIS_CHECK(near(r.asmbl.mass_properties().propellant_mass, 1580.0f, 1e-3f));
    AEGIS_CHECK(near(r.asmbl.mass_properties().total_mass, 4143.0f - 100.0f, 1e-3f));
}

AEGIS_TEST(parts, "consume_propellant clamps at the remaining propellant") {
    DemoRocket r = build_demo_rocket();
    const core::Result<f32> drained = r.asmbl.consume_propellant(r.tank2, 9999.0f);
    AEGIS_REQUIRE(drained.has_value());
    AEGIS_CHECK(near(drained.value(), 420.0f));
    AEGIS_CHECK(near(r.asmbl.mass_properties().propellant_mass, 1260.0f, 1e-3f));
}

AEGIS_TEST(parts, "consume_propellant rejects negative amounts") {
    DemoRocket r = build_demo_rocket();
    const core::Result<f32> drained = r.asmbl.consume_propellant(r.tank2, -5.0f);
    AEGIS_CHECK(!drained.has_value());
}

AEGIS_TEST(parts, "consume_propellant rejects non-tank parts") {
    DemoRocket r = build_demo_rocket();
    const core::Result<f32> drained = r.asmbl.consume_propellant(r.pod, 1.0f);
    AEGIS_CHECK(!drained.has_value());
}

AEGIS_TEST(parts, "consume_propellant rejects tombstoned parts") {
    DemoRocket r = build_demo_rocket();
    AEGIS_REQUIRE(r.asmbl.remove(r.tank2).has_value());
    const core::Result<f32> drained = r.asmbl.consume_propellant(r.tank2, 1.0f);
    AEGIS_CHECK(!drained.has_value());
}

}  // namespace
}  // namespace aegis::parts
