// Aegis2D — built-in part template registry (Milestone 7).
//
// Original, generic part designs for a 2D space-flight game. Sizes are integer
// grid cells; masses in mass units; all node geometry in unrotated cell units
// with the origin at the part's bottom-left cell.
#include <aegis/parts/part_template.hpp>

#include <array>

namespace aegis::parts {

namespace {

using enum NodeType;

// ---------------------------------------------------------------------------
// Node layouts (constexpr storage so templates can point at them)
// ---------------------------------------------------------------------------
constexpr std::array<NodeDef, 4> kPodNodes{{
    {0, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},   // attach under the pod
    {1, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},  // left hardpoint
    {2, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},   // right hardpoint
    {3, Top, 0.5f, 1.0f, 0.0f, 1.0f},       // fairing / beacon on top
}};

constexpr std::array<NodeDef, 4> kLongTankNodes{{
    {0, Top, 0.5f, 2.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 1.0f, -1.0f, 0.0f},
    {3, Lateral, 1.0f, 1.0f, 1.0f, 0.0f},
}};

constexpr std::array<NodeDef, 4> kHeavyTankNodes{{
    {0, Top, 1.0f, 4.0f, 0.0f, 1.0f},
    {1, Bottom, 1.0f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 2.0f, -1.0f, 0.0f},
    {3, Lateral, 2.0f, 2.0f, 1.0f, 0.0f},
}};

constexpr std::array<NodeDef, 4> kShortTankNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},
    {3, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},
}};

// Engines carry a top attachment plate AND an aft collar node so stage
// assemblies (engine over a separator, tank over the engine) snap with
// opposed normals at rotation 0.
constexpr std::array<NodeDef, 2> kSparrowNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 2> kLongbowNodes{{
    {0, Top, 1.0f, 2.0f, 0.0f, 1.0f},
    {1, Bottom, 1.0f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 2> kKestrelNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 2> kStandardDecouplerNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 2> kHeavySeparatorNodes{{
    {0, Top, 0.5f, 2.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 2> kRadialSeparatorNodes{{
    {0, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},
    {1, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},
}};

constexpr std::array<NodeDef, 2> kFairingShellNodes{{
    {0, Top, 0.5f, 2.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 1> kNoseFairingNodes{{
    {0, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
}};

constexpr std::array<NodeDef, 4> kRcsNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},
    {3, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},
}};

// Landing leg: attaches along the body (lateral hardpoints) or between stacked
// stages (top/bottom). Pure mass — no flight effect beyond mass/inertia.
constexpr std::array<NodeDef, 4> kLandingLegNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},
    {3, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},
}};

constexpr std::array<NodeDef, 4> kStrutNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},
    {3, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},
}};

// ---------------------------------------------------------------------------
// Template table — ids 0..15 (order is the id).
// ---------------------------------------------------------------------------
constexpr std::array<PartTemplate, kTemplateCount> kTemplates{{
    {0, "Aegis MK-I Pod", PartCategory::CommandPod, 1, 1, 800.0f, {}, {}, {}, {}, kPodNodes, "mk1_pod"},
    {1, "Long-2 Tank", PartCategory::FuelTank, 1, 2, 380.0f, {1400.0f}, {}, {}, {}, kLongTankNodes, "long_2_tank"},
    {2, "Long-4 Tank", PartCategory::FuelTank, 2, 4, 950.0f, {6200.0f}, {}, {}, {}, kHeavyTankNodes, "long_4_tank"},
    {3, "Short-1 Tank", PartCategory::FuelTank, 1, 1, 160.0f, {420.0f}, {}, {}, {}, kShortTankNodes, "short_1_tank"},
    {4, "Sparrow Engine", PartCategory::Engine, 1, 1, 230.0f, {}, {60000.0f, 295.0f}, {}, {}, kSparrowNodes, "sparrow_engine"},
    {5, "Longbow Engine", PartCategory::Engine, 2, 2, 690.0f, {}, {850000.0f, 318.0f}, {}, {}, kLongbowNodes, "longbow_engine"},
    {6, "Kestrel Engine", PartCategory::Engine, 1, 1, 95.0f, {}, {24000.0f, 265.0f}, {}, {}, kKestrelNodes, "kestrel_engine"},
    {7, "MK-1 Decoupler", PartCategory::Decoupler, 1, 1, 55.0f, {}, {}, {1.5f}, {}, kStandardDecouplerNodes, "mk1_decoupler"},
    {8, "Heavy Separator", PartCategory::Decoupler, 1, 2, 190.0f, {}, {}, {2.2f}, {}, kHeavySeparatorNodes, "heavy_separator"},
    {9, "Radial Separator", PartCategory::Decoupler, 1, 1, 48.0f, {}, {}, {1.2f}, {}, kRadialSeparatorNodes, "radial_separator"},
    {10, "Fairing Shell", PartCategory::Fairing, 1, 2, 75.0f, {}, {}, {}, {}, kFairingShellNodes, "fairing_shell"},
    {11, "Nose Fairing", PartCategory::Fairing, 1, 1, 32.0f, {}, {}, {}, {}, kNoseFairingNodes, "nose_fairing"},
    {12, "Strut Brace", PartCategory::Strut, 1, 1, 40.0f, {}, {}, {}, {}, kStrutNodes, "strut_brace"},
    {13, "RCS Block", PartCategory::RcsBlock, 1, 1, 120.0f, {}, {}, {}, {52000.0f, 0.9f}, kRcsNodes, "rcs_block"},
    {14, "Monoprop Tank", PartCategory::FuelTank, 1, 1, 60.0f, {140.0f, 1}, {}, {}, {}, kShortTankNodes, "monoprop_tank"},
    {15, "Landing Leg", PartCategory::Strut, 1, 1, 90.0f, {}, {}, {}, {}, kLandingLegNodes, "landing_leg"},
}};

}  // namespace

const std::array<PartTemplate, kTemplateCount>& templates() noexcept { return kTemplates; }

const PartTemplate* find_template(const TemplateId id) noexcept {
    if (id >= kTemplateCount) {
        return nullptr;
    }
    return &kTemplates[id];
}

const PartTemplate* find_template_by_key(const std::string_view key) noexcept {
    for (const PartTemplate& template_def : kTemplates) {
        if (key == template_def.key) {
            return &template_def;
        }
    }
    return nullptr;
}

// Sanity invariants for the built-in table (verified by unit tests as well).
static_assert([] {
    for (std::size_t i = 0; i < kTemplateCount; ++i) {
        if (kTemplates[i].id != i) {
            return false;
        }
    }
    return true;
}(), "built-in template ids must match table order");

}  // namespace aegis::parts
