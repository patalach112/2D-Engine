// Aegis2D — craft save/load (Milestone 10): JSON snapshots of a VAB build.
//
// A craft file captures everything needed to reproduce an assembly exactly:
//   * the parts (template key, grid cells, rotation/flip, tank fill),
//   * the staging assignments (part, stage number),
//   * free-form meta (name, engine version, milestone at save time).
//
// The format is stable, human-readable and diff-friendly. Unknown template
// keys, malformed geometry (overlap) or invalid staging targets are hard
// errors — a failed load never mutates the live assembly: craft_from_json
// builds into fresh state and the caller swaps on success.
//
// Identity: parts reference template `key` strings, never runtime TemplateIds.
// Staging references parts by their index in the file's parts array, and the
// fresh loader assembly preserves file order, so indices are stable.
#pragma once

#include <aegis/export.h>

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>

#include <string>
#include <string_view>

namespace aegis::serial {

inline constexpr core::u32 kCraftFormatVersion = 1;
inline constexpr const char* kCraftKind = "aegis-craft";

struct AEGIS_API CraftMeta {
    std::string name = "CUSTOM CRAFT";   ///< free-form display name
    std::string engine;                  ///< kVersionString at save time
    core::u32 milestone = 0;             ///< kMilestone at save time
};

struct AEGIS_API Craft {
    parts::VehicleAssembly assembly;
    parts::StagingList staging;
    CraftMeta meta;
};

/// Serializes the CURRENT build: live parts only (tombstoned slots are
/// skipped), tank fill levels, and staging assignments. Deterministic — the
/// same assembly always yields byte-identical output.
[[nodiscard]] AEGIS_API std::string craft_to_json(const parts::VehicleAssembly& assembly,
                                        const parts::StagingList& staging,
                                        const CraftMeta& meta);

/// Parses and rebuilds a craft. Strictly validated; on any failure the error
/// message explains the problem and no assembly state escapes the call.
[[nodiscard]] AEGIS_API core::Result<Craft> craft_from_json(std::string_view text);

}  // namespace aegis::serial
