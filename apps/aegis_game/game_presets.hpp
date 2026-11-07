// Aegis2D — built-in craft presets for the aegis_game VAB (Milestone 9).
//
// Three deterministic, grid-legal stacks spanning the game's flight envelope:
//   0  SPARROW HOPPER     — 6 parts, 3675 kg: the two-stage hopper that ships
//                           in the VAB. Lifts on a Sparrow, separates a MK-1
//                           Decoupler, coasts on the upper Sparrow + Short-1.
//   1  TWIN-TANK ORBITER  — 7 parts, 5320 kg: Sparrow booster over two
//                           Long-2s, MK-1 Decoupler, Kestrel upper stage on a
//                           Short-1. Slowest to leave the pad (TWR ~1.15) but
//                           the highest Δv in the fleet.
//   2  RADIAL TRIPLE      — 12 parts, 4191 kg: three Sparrows lit from the
//                           pad (single + two radial boosters on Radial
//                           Separators), core Short-1 core, Kestrel upper.
//                           The booster staging beat — all three engines
//                           fire at launch, separators clear everything at
//                           stage 1.
//
// Determinism: identical calls produce identical assemblies (index order is
// insertion order; no clocks, no randomness). All parts are 1-wide so grid
// alignment holds at every node.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/parts/vehicle_assembly.hpp>

namespace aegis::game {

inline constexpr core::u32 kPresetCount = 3;

struct CraftPresetInfo {
    const char* name;   ///< upper-case VAB label, e.g. "SPARROW HOPPER"
    const char* blurb;  ///< one-line description for the info panel
    core::u32 part_count;
    core::f64 mass_kg;
};

[[nodiscard]] const CraftPresetInfo& preset_info(core::u32 index) noexcept;

/// Build the preset assembly. Out-of-range indices wrap modulo kPresetCount.
[[nodiscard]] parts::VehicleAssembly make_preset_rocket(core::u32 index) noexcept;

}  // namespace aegis::game
