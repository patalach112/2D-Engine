// Aegis2D — staging: ordered per-part actions with sequential activation
// (Milestone 7).
//
// A Stage is a firing order index; within a stage, actions fire in a fixed
// kind order — FireDecoupler first (stable by part index), then
// JettisonFairing, then IgniteEngine — matching SFS's separation-before-
// ignition convention. Indices survive decoupler splits (assemblies carry
// tombstoned gaps), so assignments stay valid across activations.
#pragma once

#include <aegis/export.h>

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/parts/vehicle_assembly.hpp>

#include <unordered_map>
#include <vector>

namespace aegis::parts {

using core::u8;

enum class StageAction : u8 { IgniteEngine, FireDecoupler, JettisonFairing };

struct AEGIS_API StageActionItem {
    PartIndex part = kInvalidPart;
    StageAction action = StageAction::IgniteEngine;
};

struct AEGIS_API Stage {
    std::vector<StageActionItem> items{};
};

inline constexpr u8 kUnstaged = 0xFF;
inline constexpr u8 kMaxStages = 16;

struct AEGIS_API StageFireResult {
    /// Actions that fired, in firing order (decouplers → fairings → engines).
    std::vector<StageActionItem> fired{};

    /// Components split off by FireDecoupler actions, in order of detachment,
    /// lowest original part index first. The pod-bearing component is adopted
    /// into the assembly passed to activate_next_stage().
    std::vector<VehicleAssembly> detached{};
};

class AEGIS_API StagingList {
public:
    // --- Assignment ---------------------------------------------------------

    /// Assign (or re-assign) a stage to a stageable part (Engine, Decoupler,
    /// Fairing). Tanks, pods and struts are rejected, as are stage >= kMaxStages.
    [[nodiscard]] core::Result<void> assign(const VehicleAssembly& asmbl, PartIndex part, u8 stage);

    /// Drop assignments for parts that are dead (tombstoned) in `asmbl` and
    /// rebuild the dense stage list. Call after any structural removal so
    /// orphaned-but-staged parts cannot leave phantom stages behind.
    /// Returns the number of assignments pruned.
    core::u32 prune(const VehicleAssembly& asmbl);

    /// Remove any assignment for a part.
    [[nodiscard]] core::Result<void> unassign(PartIndex part);

    [[nodiscard]] bool is_staged(PartIndex part) const noexcept;
    [[nodiscard]] u8 stage_of(PartIndex part) const noexcept;  ///< kUnstaged when unassigned
    [[nodiscard]] u8 stage_count() const noexcept;
    [[nodiscard]] const std::vector<Stage>& stages() const noexcept { return stages_; }

    /// Automatic stack-depth staging heuristic:
    ///   depth(part) = number of live decouplers strictly below part (world y);
    ///   engines/fairings are assigned 2*depth, decouplers 2*depth + 1.
    /// The bottom-most engine group is stage 0 (liftoff). Deterministic;
    /// manual assign() overrides everything.
    [[nodiscard]] u8 auto_assign(const VehicleAssembly& asmbl);

    // --- Activation -----------------------------------------------------------

    /// Fire the next pending stage against `asmbl`:
    ///   1. FireDecoupler actions (in firing order) cut incident adjacency
    ///      edges; the assembly is then split and the pod-bearing component is
    ///      adopted back into `asmbl`; other components go to `detached`.
    ///   2. JettisonFairing actions flood-fill the alive Fairing-category
    ///      cluster seeded by the staged fairings (BFS stops at non-fairing
    ///      parts, so a payload inside stays), detach it as a ballistic
    ///      free-flyer via split_members() and adopt the remainder.
    ///   3. IgniteEngine actions are recorded only (M8 owns thrust).
    /// Returns the fired actions; advances the cursor once per call. Fails
    /// when the list is exhausted.
    [[nodiscard]] core::Result<StageFireResult> activate_next_stage(VehicleAssembly& asmbl);

    [[nodiscard]] u8 cursor() const noexcept { return cursor_; }
    void reset_cursor() noexcept { cursor_ = 0; }
    void clear_all() noexcept;

private:
    void rebuild_stages();

    std::unordered_map<u32, u8> assignments_;            // part -> stage
    std::unordered_map<u32, StageAction> action_kinds_;  // part -> action (set at assign time)
    std::vector<Stage> stages_;                          // dense, in firing order
    u8 cursor_ = 0;
};

}  // namespace aegis::parts
