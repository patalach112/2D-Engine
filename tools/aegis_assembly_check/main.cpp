// Aegis2D — aegis_assembly_check: headless Milestone-7 verification tool.
//
// Builds a deterministic three-stage rocket (pod, fairing, two radial
// boosters via the mirror_snap symmetry helper), stages it through the full
// firing sequence, and checks the invariants of the whole assembly pipeline.
// Exits 0 on success, 1 on any failed check; prints ROCKET/ASSEMBLY_VERIFIED
// on success for CI log scraping.
#include <aegis/core/log.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aegis;
using namespace aegis::parts;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, what)                                                              \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::fprintf(stderr, "FAIL [%d] %s\n", __LINE__, (what));                   \
        }                                                                              \
    } while (0)

// Template ids (order in the built-in registry).
constexpr TemplateId kPod = 0;
constexpr TemplateId kLongTank2 = 1;
constexpr TemplateId kLongTank4 = 2;
constexpr TemplateId kShortTank = 3;
constexpr TemplateId kSparrow = 4;
constexpr TemplateId kLongbow = 5;
constexpr TemplateId kKestrel = 6;
constexpr TemplateId kDecoupler = 7;
constexpr TemplateId kHeavySeparator = 8;
constexpr TemplateId kRadialSeparator = 9;
constexpr TemplateId kFairingShell = 10;
constexpr TemplateId kNoseFairing = 11;
constexpr TemplateId kStrut = 12;

static constexpr char kActionName[][20] = {"IGNITE", "DECOUPLE", "JETTISON"};

static void print_assembly(const VehicleAssembly& asmbl, const char* label) {
    std::printf("=== %s — %u parts ===\n", label, asmbl.alive_count());
    for (std::size_t i = 0; i < asmbl.parts().size(); ++i) {
        const Part* p = asmbl.part(static_cast<PartIndex>(i));
        if (!p) {
            continue;  // tombstone
        }
        const PartTemplate* tmpl = find_template(p->tmpl);
        const std::array<f32, 2> c = asmbl.part_center_wu(static_cast<PartIndex>(i));
        std::printf("  [%2zu] %-18s grid=(%d,%d) rot=%u  center=(%.2f, %.2f)  prop=%.0f\n", i,
                    tmpl->name, p->gx, p->gy, p->rot, static_cast<double>(c[0]),
                    static_cast<double>(c[1]), static_cast<double>(p->propellant));
    }
    const MassProperties& mp = asmbl.mass_properties();
    std::printf("  dry=%.1f  prop=%.1f  total=%.1f  CoM=(%.3f, %.3f)  Izz=%.2f\n",
                static_cast<double>(mp.dry_mass), static_cast<double>(mp.propellant_mass),
                static_cast<double>(mp.total_mass), static_cast<double>(mp.center_of_mass[0]),
                static_cast<double>(mp.center_of_mass[1]), static_cast<double>(mp.inertia_izz));
}

static void print_stages(const StagingList& staging) {
    std::printf("=== stages: %u (cursor=%u) ===\n", staging.stage_count(), staging.cursor());
    const std::vector<Stage>& stages = staging.stages();
    for (std::size_t s = 0; s < stages.size(); ++s) {
        std::printf("  stage %zu:", s);
        for (const StageActionItem& item : stages[s].items) {
            std::printf(" [%s part=%u]", kActionName[static_cast<u8>(item.action)], item.part);
        }
        std::printf("\n");
    }
}

static void print_result(const StageFireResult& result, std::size_t step) {
    std::printf("--- activate_next_stage #%zu ---\n", step);
    for (const StageActionItem& item : result.fired) {
        std::printf("  fired: %s part=%u\n", kActionName[static_cast<u8>(item.action)], item.part);
    }
    std::printf("  detached components: %zu\n", result.detached.size());
    for (std::size_t i = 0; i < result.detached.size(); ++i) {
        std::printf("  detached[%zu]: %u parts, total=%.1f\n", i, result.detached[i].alive_count(),
                    static_cast<double>(result.detached[i].mass_properties().total_mass));
    }
}

int main() {
    VehicleAssembly asmbl;

    // ------------------------------------------------------------------
    // Build the rocket bottom-up (all rotation 0, every attach opposed):
    //
    //   y=7:  Nose Fairing
    //   y=6:  Aegis MK-I Pod
    //   y=5:  Short-1 Tank        (stage 3)
    //   y=4:  MK-1 Decoupler      (sep 2)
    //   y=3:  Short-1 Tank        (stage 2)  <- boosters at this level
    //   y=2:  Kestrel Engine      (stage 2)
    //   y=1:  MK-1 Decoupler      (sep 1)
    //   y=0:  Sparrow Engine      (liftoff / stage 1)
    //
    // Boosters (x=-2 and x=+2): Sparrow under Short-1 Tank, joined to the
    // stage-2 tank through radial separators at (±1, 3).
    // ------------------------------------------------------------------
    auto add = [&asmbl](TemplateId tmpl, i16 gx, i16 gy, u8 rot) {
        core::Result<PartIndex> r = asmbl.add_root(tmpl, gx, gy, rot);
        if (!r.has_value()) {
            std::fprintf(stderr, "add_root failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
        return r.value();
    };
    auto snap = [&asmbl](PartIndex parent, NodeId pnode, TemplateId child, NodeId cnode, u8 rot) {
        core::Result<PartIndex> r = asmbl.snap(parent, pnode, child, cnode, rot);
        if (!r.has_value()) {
            std::fprintf(stderr, "snap failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
        return r.value();
    };

    const PartIndex liftoff = add(kSparrow, 0, 0, 0);                     // 0
    const PartIndex sep1 = snap(liftoff, 0, kDecoupler, 1, 0);            // 1
    const PartIndex kestrel = snap(sep1, 0, kKestrel, 1, 0);              // 2 (on top of sep1's bottom-attach)
    const PartIndex tank2 = snap(kestrel, 0, kShortTank, 1, 0);           // 3
    const PartIndex sep2 = snap(tank2, 0, kDecoupler, 1, 0);              // 4
    const PartIndex tank3 = snap(sep2, 0, kShortTank, 1, 0);              // 5
    const PartIndex pod = snap(tank3, 0, kPod, 0, 0);                     // 6
    const PartIndex fairing = snap(pod, 3, kNoseFairing, 0, 0);           // 7

    // Boosters: radial separator on tank2's side nodes, tank+Sparrow above it
    // via the mirror symmetry helper.
    const PartIndex sep_left = snap(tank2, 2, kRadialSeparator, 1, 0);    // 8
    const PartIndex booster_left_tank = snap(sep_left, 0, kShortTank, 3, 0);  // 9
    const PartIndex booster_left_engine = snap(booster_left_tank, 1, kSparrow, 0, 0);  // 10
    const core::Result<PartIndex> sep_right_res = asmbl.mirror_snap(
        tank2, 2, kRadialSeparator, 1, 0, Axis::Vertical, 0.5f);
    if (!sep_right_res.has_value()) {
        std::fprintf(stderr, "mirror_snap failed: %s\n", sep_right_res.error().message.c_str());
        return 1;
    }
    const PartIndex sep_right = sep_right_res.value();                    // 11
    // The mirrored separator exposes its outward (+x) node as id 0.
    const PartIndex booster_right_tank = snap(sep_right, 0, kShortTank, 2, 0);  // 12
    const PartIndex booster_right_engine = snap(booster_right_tank, 1, kSparrow, 0, 0);  // 13

    CHECK(asmbl.alive_count() == 14, "14 live parts after build");
    CHECK(asmbl.validate().empty(), "fresh rocket must validate clean");
    CHECK(asmbl.mass_properties().total_mass == 800.0f + 160.0f * 4 + 420.0f * 4 + 230.0f * 3 +
                                                    95.0f + 55.0f * 2 + 48.0f * 2 + 32.0f,
          "exact live mass of the fresh rocket");
    CHECK(asmbl.mass_properties().propellant_mass == 420.0f * 4, "four loaded tanks");
    CHECK(asmbl.mass_properties().dry_mass > 800.0f, "dry mass at least pod + engines");

    // Symmetry helper: mirrored right-hand stack sits at positive x, mirrored.
    {
        const std::array<f32, 2> c_left = asmbl.part_center_wu(sep_left);
        const std::array<f32, 2> c_right = asmbl.part_center_wu(sep_right);
        CHECK(std::fabs(c_left[0] + c_right[0] - 1.0f) < 1e-3f, "separators mirrored about x=0.5 wu");
        CHECK(c_left[1] == c_right[1], "booster separators at identical height");
        const std::array<f32, 2> e_left = asmbl.part_center_wu(booster_left_engine);
        const std::array<f32, 2> e_right = asmbl.part_center_wu(booster_right_engine);
        CHECK(std::fabs(e_left[0] + e_right[0] - 1.0f) < 1e-3f, "booster engines mirrored about x=0.5 wu");
        CHECK(asmbl.part(sep_right)->rot == kFlipBit, "mirrored separator carries the flip bit");
    }

    print_assembly(asmbl, "fresh rocket");

    // ------------------------------------------------------------------
    // Staging: show the automatic heuristic, then set the flight plan by hand.
    // ------------------------------------------------------------------
    StagingList staging;
    const u8 auto_stages = staging.auto_assign(asmbl);
    CHECK(auto_stages >= 3, "auto staging produces a multi-stage flight plan");
    print_stages(staging);
    staging.clear_all();

    auto assign = [&](PartIndex part, u8 stage) {
        core::Result<void> r = staging.assign(asmbl, part, stage);
        if (!r.has_value()) {
            std::fprintf(stderr, "assign failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
    };
    assign(liftoff, 0);
    assign(booster_left_engine, 0);
    assign(booster_right_engine, 0);
    assign(sep1, 1);
    assign(sep_left, 2);
    assign(sep_right, 2);
    assign(kestrel, 3);
    assign(sep2, 4);
    assign(fairing, 5);
    CHECK(staging.stage_count() == 6, "manual flight plan has 6 stages");
    CHECK(staging.stage_of(sep2) == 4, "sep2 assigned to stage 4");
    CHECK(staging.stage_of(tank2) == kUnstaged, "tanks are not stageable");
    print_stages(staging);

    auto activate = [&]() -> StageFireResult {
        core::Result<StageFireResult> r = staging.activate_next_stage(asmbl);
        if (!r.has_value()) {
            std::fprintf(stderr, "activate failed: %s\n", r.error().message.c_str());
            std::exit(1);
        }
        return std::move(r.value());
    };

    std::size_t step = 0;

    // Stage 0: liftoff — three engines ignite, nothing detaches.
    {
        StageFireResult r = activate();
        print_result(r, ++step);
        CHECK(r.fired.size() == 3, "stage 0 fires the three liftoff engines");
        CHECK(r.detached.empty(), "stage 0 detaches nothing");
        for (const StageActionItem& item : r.fired) {
            CHECK(item.action == StageAction::IgniteEngine, "stage 0 is engines only");
        }
    }

    // Stage 1: inter-stage decoupler fires; liftoff stage separates.
    {
        StageFireResult r = activate();
        print_result(r, ++step);
        CHECK(r.fired.size() == 1 && r.fired[0].action == StageAction::FireDecoupler,
              "stage 1 fires the inter-stage decoupler");
        CHECK(r.detached.size() == 1, "stage 1 detaches exactly one component");
        if (!r.detached.empty()) {
            CHECK(r.detached[0].alive_count() == 2, "detached stage-1 is Sparrow + decoupler");
            CHECK(r.detached[0].is_alive(liftoff), "detached stage-1 holds the liftoff engine");
            CHECK(!r.detached[0].validate().empty(), "detached stage has no command pod");
        }
        CHECK(asmbl.alive_count() == 12, "vehicle keeps 12 parts after sep 1");
        print_assembly(asmbl, "after stage 1");
    }

    // Stage 2: radial separators fire; both boosters detach together.
    {
        StageFireResult r = activate();
        print_result(r, ++step);
        CHECK(r.fired.size() == 2, "stage 2 fires both radial separators");
        CHECK(r.detached.size() == 2, "stage 2 detaches both booster stacks");
        std::size_t detached_parts = 0;
        for (const VehicleAssembly& piece : r.detached) {
            detached_parts += piece.alive_count();
        }
        CHECK(detached_parts == 4, "each detached booster is tank + engine");
        CHECK(asmbl.alive_count() == 8, "vehicle keeps 8 parts after boosters leave");
        CHECK(asmbl.validate().empty(), "vehicle valid after booster jettison");
        print_assembly(asmbl, "after stage 2");
    }

    // Stage 3: Kestrel ignition (upper stage).
    {
        StageFireResult r = activate();
        print_result(r, ++step);
        CHECK(r.fired.size() == 1 && r.fired[0].action == StageAction::IgniteEngine,
              "stage 3 ignites the Kestrel");
        CHECK(r.fired.size() == 1 && r.fired[0].part == kestrel, "stage 3 ignites the Kestrel part");
        CHECK(r.detached.empty(), "stage 3 detaches nothing");
    }

    // Stage 4: inter-stage decoupler fires again; stage-2 unit separates.
    {
        StageFireResult r = activate();
        print_result(r, ++step);
        CHECK(r.fired.size() == 1 && r.fired[0].action == StageAction::FireDecoupler,
              "stage 4 fires the upper-stage decoupler");
        CHECK(r.detached.size() == 1, "stage 4 detaches one component");
        if (!r.detached.empty()) {
            CHECK(r.detached[0].alive_count() == 5,
                  "detached stage-2 is Kestrel + tank2 + two carcasses + sep2");
        }
        CHECK(asmbl.alive_count() == 3, "vehicle keeps tank3, pod and fairing");
        print_assembly(asmbl, "after stage 4");
    }

    // Stage 5: nose fairing jettisoned.
    {
        StageFireResult r = activate();
        print_result(r, ++step);
        CHECK(r.fired.size() == 1 && r.fired[0].action == StageAction::JettisonFairing,
              "stage 5 jettisons the nose fairing");
        CHECK(!asmbl.is_alive(fairing), "fairing part tombstoned");
        CHECK(asmbl.validate().empty(), "vehicle still valid after fairing jettison");
    }

    // Cursor exhausted: further activation must fail cleanly.
    {
        core::Result<StageFireResult> r = staging.activate_next_stage(asmbl);
        CHECK(!r.has_value(), "activation past the last stage fails");
    }

    // Final invariants: pod + one full tank.
    const MassProperties& mp = asmbl.mass_properties();
    std::printf("=== final vehicle: total=%.1f CoM=(%.3f, %.3f) Izz=%.2f ===\n",
                static_cast<double>(mp.total_mass), static_cast<double>(mp.center_of_mass[0]),
                static_cast<double>(mp.center_of_mass[1]), static_cast<double>(mp.inertia_izz));
    CHECK(asmbl.validate().empty(), "final vehicle validates clean");
    CHECK(mp.total_mass == 800.0f + 160.0f + 420.0f, "final mass is pod + tank3 with full load");
    CHECK(asmbl.alive_count() == 2, "final vehicle is pod + tank3");

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures != 0) {
        std::printf("ROCKET/ASSEMBLY_FAILED\n");
        return 1;
    }
    std::printf("ROCKET/ASSEMBLY_VERIFIED\n");
    return 0;
}
