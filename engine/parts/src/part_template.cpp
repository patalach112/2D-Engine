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

constexpr std::array<NodeDef, 4> kStrutNodes{{
    {0, Top, 0.5f, 1.0f, 0.0f, 1.0f},
    {1, Bottom, 0.5f, 0.0f, 0.0f, -1.0f},
    {2, Lateral, 0.0f, 0.5f, -1.0f, 0.0f},
    {3, Lateral, 1.0f, 0.5f, 1.0f, 0.0f},
}};

// ---------------------------------------------------------------------------
// Template table — ids 0..12 (order is the id).
// ---------------------------------------------------------------------------
constexpr std::array<PartTemplate, kTemplateCount> kTemplates{{
    {0, "Aegis MK-I Pod", PartCategory::CommandPod, 1, 1, 800.0f, {}, {}, {}, kPodNodes},
    {1, "Long-2 Tank", PartCategory::FuelTank, 1, 2, 380.0f, {1400.0f}, {}, {}, kLongTankNodes},
    {2, "Long-4 Tank", PartCategory::FuelTank, 2, 4, 950.0f, {6200.0f}, {}, {}, kHeavyTankNodes},
    {3, "Short-1 Tank", PartCategory::FuelTank, 1, 1, 160.0f, {420.0f}, {}, {}, kShortTankNodes},
    {4, "Sparrow Engine", PartCategory::Engine, 1, 1, 230.0f, {}, {60000.0f, 295.0f}, {}, kSparrowNodes},
    {5, "Longbow Engine", PartCategory::Engine, 2, 2, 690.0f, {}, {850000.0f, 318.0f}, {}, kLongbowNodes},
    {6, "Kestrel Engine", PartCategory::Engine, 1, 1, 95.0f, {}, {24000.0f, 265.0f}, {}, kKestrelNodes},
    {7, "MK-1 Decoupler", PartCategory::Decoupler, 1, 1, 55.0f, {}, {}, {1.5f}, kStandardDecouplerNodes},
    {8, "Heavy Separator", PartCategory::Decoupler, 1, 2, 190.0f, {}, {}, {2.2f}, kHeavySeparatorNodes},
    {9, "Radial Separator", PartCategory::Decoupler, 1, 1, 48.0f, {}, {}, {1.2f}, kRadialSeparatorNodes},
    {10, "Fairing Shell", PartCategory::Fairing, 1, 2, 75.0f, {}, {}, {}, kFairingShellNodes},
    {11, "Nose Fairing", PartCategory::Fairing, 1, 1, 32.0f, {}, {}, {}, kNoseFairingNodes},
    {12, "Strut Brace", PartCategory::Strut, 1, 1, 40.0f, {}, {}, {}, kStrutNodes},
}};

}  // namespace

const std::array<PartTemplate, kTemplateCount>& templates() noexcept { return kTemplates; }

const PartTemplate* find_template(const TemplateId id) noexcept {
    if (id >= kTemplateCount) {
        return nullptr;
    }
    return &kTemplates[id];
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
