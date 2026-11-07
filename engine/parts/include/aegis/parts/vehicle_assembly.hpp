// Aegis2D — vehicle assembly: grid placement, node snapping, staging splits,
// mass properties (Milestone 7).
//
// SFS-style discrete grid assembly with full data-oriented storage: parts are
// 16-byte PODs in one contiguous vector, occupancy is a cell→index map, and
// adjacency is derived on demand (never stored). The command-pod tree is the
// flight vehicle; staging logic lives in staging.hpp.
#pragma once

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/parts/part_template.hpp>

#include <array>
#include <unordered_map>
#include <vector>

namespace aegis::parts {

/// Node-type compatibility rule for snap() (top<->bottom, lateral<->lateral,
/// any Surface<->any node type). Public so build tooling (VAB) can mirror it.
[[nodiscard]] inline bool nodes_compatible(const NodeType a, const NodeType b) noexcept {
    if (a == NodeType::Surface || b == NodeType::Surface) {
        return true;
    }
    if ((a == NodeType::Top && b == NodeType::Bottom) || (a == NodeType::Bottom && b == NodeType::Top)) {
        return true;
    }
    return a == NodeType::Lateral && b == NodeType::Lateral;
}

using core::f32;
using core::f64;
using core::i16;
using core::i64;
using core::u8;
using core::u32;
using core::u64;
using core::u16;

inline constexpr u8 kFuelLiquid = 0;
inline constexpr u8 kFuelMonoprop = 1;  ///< RCS feed stock; main engines never draw from it
inline constexpr std::size_t kMaxCellsPerPart = 64;
inline constexpr std::size_t kMaxParts = 4096;
using PartIndex = u32;
inline constexpr PartIndex kInvalidPart = ~u32{0};

/// 16-byte POD part instance. Tombstoned slots (tmpl == kInvalidTemplate) are
/// never reused: part indices stay stable for staging and M8 flight dynamics.
struct Part {
    TemplateId tmpl = kInvalidTemplate;
    i16 gx = 0, gy = 0;   ///< grid cell of the rotated footprint origin (bottom-left)
    u8 rot = 0;           ///< low nibble CCW steps, bit 4 = flip
    u8 fuel_type = kFuelLiquid;
    u8 reserved1 = 0;
    u8 reserved2 = 0;
    f32 propellant = 0.0f;  ///< current propellant mass (tanks; full at placement)
};
static_assert(sizeof(Part) == 16, "Part must stay a 16-byte POD");

/// Rotated footprint of a template under a rotation step.
struct Footprint {
    static constexpr std::size_t kMaxCells = kMaxCellsPerPart;
    std::array<std::pair<i16, i16>, kMaxCellsPerPart> cells{};
    u8 count = 0;
    u8 w = 1, h = 1;  ///< rotated dimensions in cells
};

/// Transformed geometry of a node (positions in CELL units relative to the
/// rotated footprint origin).
[[nodiscard]] Footprint footprint_of(u8 w, u8 h, u8 rot) noexcept;
[[nodiscard]] std::pair<u8, u8> rotated_dims(u8 w, u8 h, u8 rot) noexcept;
/// Node position/normal in cell units, rotated into the part's rotated frame.
[[nodiscard]] std::pair<std::array<f32, 2>, std::array<f32, 2>> transform_node(const NodeDef& node,
                                                                              u8 w, u8 h, u8 rot) noexcept;

struct MassProperties {
    f32 dry_mass = 0.0f;
    f32 propellant_mass = 0.0f;
    f32 total_mass = 0.0f;
    std::array<f32, 2> center_of_mass{};  ///< wu
    f32 inertia_izz = 0.0f;               ///< point-mass Izz about CoM (scalar in 2D)
};

/// Mirror axis for the symmetry helper. Vertical mirrors x about `axis_wu`;
/// Horizontal mirrors y about `axis_wu`.
enum class Axis : u8 { Vertical, Horizontal };

struct AssemblyIssue {
    enum class Kind : u8 { NoCommandPod, UnreachablePart };
    Kind kind = Kind::NoCommandPod;
    PartIndex part = kInvalidPart;
};

[[nodiscard]] u64 hash_cell(i16 gx, i16 gy) noexcept;

class VehicleAssembly {
public:
    VehicleAssembly() noexcept = default;

    // --- Building ----------------------------------------------------------

    /// Place a new root part (no snap check; typically the command pod).
    [[nodiscard]] core::Result<PartIndex> add_root(TemplateId tmpl, i16 gx, i16 gy, u8 rot);

    /// Snap a child part onto a parent's node. Returns the new part's index.
    /// The child node is identified by index into its template's node table.
    [[nodiscard]] core::Result<PartIndex> snap(PartIndex parent, NodeId parent_node,
                                               TemplateId child, NodeId child_node, u8 rot);

    /// Mirror counterpart of a snapped child across `axis` (world units).
    /// Finds the parent-part node that is the mirror image of `parent_node`
    /// (position mirrored, normal mirrored) and snaps the child there with the
    /// mirrored rotation. Deterministic; for x4 symmetry call with both axes.
    [[nodiscard]] core::Result<PartIndex> mirror_snap(PartIndex parent, NodeId parent_node,
                                                      TemplateId child, NodeId child_node,
                                                      u8 rot, Axis axis, f32 axis_wu);

    /// Remove a part and every part that is only attached through it
    /// (subtree cut). Returns the number of parts removed (tombstoned).
    [[nodiscard]] core::Result<u32> remove(PartIndex index);

    /// Drain propellant from a fuel tank (mass units). Returns the amount
    /// actually drained (clamped to the remaining load) and marks the mass
    /// properties dirty so CoM/Izz re-aggregate on next query. Rejects dead
    /// parts, non-tank templates and negative/NaN drains.
    [[nodiscard]] core::Result<f32> consume_propellant(PartIndex tank, f32 kg) noexcept;

    // --- Queries ------------------------------------------------------------

    [[nodiscard]] const std::vector<Part>& parts() const noexcept { return parts_; }
    [[nodiscard]] const Part* part(PartIndex index) const noexcept;
    [[nodiscard]] const PartTemplate* template_of(PartIndex index) const noexcept;
    [[nodiscard]] bool is_alive(PartIndex index) const noexcept;
    [[nodiscard]] u32 alive_count() const noexcept { return alive_count_; }

    /// World-space (wu) centre of a part's rotated footprint.
    [[nodiscard]] std::array<f32, 2> part_center_wu(PartIndex index) const noexcept;

    /// Aggregate mass properties; rebuilt lazily after any mutation.
    [[nodiscard]] const MassProperties& mass_properties() const noexcept;

    /// Derived adjacency edges (sorted, deduplicated). Costs O(nodes·log).
    [[nodiscard]] std::vector<std::pair<u32, u32>> adjacency() const;

    /// Flight-readiness checks: at least one command pod; every live part
    /// connected to a pod through live adjacency.
    [[nodiscard]] std::vector<AssemblyIssue> validate() const;

    /// Replace this assembly's contents with `other` (staging splits).
    void adopt(VehicleAssembly&& other) noexcept;

    /// Tombstone ONLY the given fairing parts (no orphan cut — dropping a nose
    /// fairing must not orphan the vehicle below it). Non-fairing indices fail.
    [[nodiscard]] core::Result<u32> jettison_fairings(const std::vector<u32>& parts_to_drop) noexcept;

    // --- Splitting (decouplers) -------------------------------------------

    /// Cut every adjacency edge incident to any of `decoupler_parts`, then
    /// partition the remaining graph into assemblies by connectivity.
    /// Returns all components (sorted by smallest original part index). The
    /// pod-bearing components are the flight vehicle(s); callers select.
    [[nodiscard]] std::vector<VehicleAssembly> split_at_decouplers(
        const std::vector<u32>& decoupler_parts) const;

    /// Detach exactly `members` (alive parts) from the assembly into a single
    /// gap-tombstoned child component. The caller splits fired decouplers
    /// first and adopts the pod-bearing component, then detaches fairing
    /// clusters the same way — original PartIndices are preserved in both.
    /// Returns {this-with-members-tombstoned, members-only assembly}; empty
    /// `members` yields no assemblies.
    [[nodiscard]] std::vector<VehicleAssembly> split_members(const std::vector<u32>& members) const;

    void clear() noexcept;

private:
    [[nodiscard]] core::Result<PartIndex> place_unchecked(TemplateId tmpl, i16 gx, i16 gy,
                                                          u8 rot) noexcept;
    void rebuild_mass_properties() const noexcept;

    std::vector<Part> parts_;
    std::unordered_map<u64, PartIndex> occupancy_;  // hash_cell -> live part slot
    u32 alive_count_ = 0;
    mutable MassProperties mass_cache_{};
    mutable bool mass_dirty_ = true;
};


/// Build the built-in demonstration rocket (bottom -> top):
///   Sparrow engine / Long-2 tank / MK-1 decoupler / Sparrow engine /
///   Short-1 tank / Aegis MK-I pod. Staging is NOT assigned — call auto_assign().
/// Deterministic; the VAB loads this on first open.
[[nodiscard]] VehicleAssembly make_demo_rocket() noexcept;

}  // namespace aegis::parts
