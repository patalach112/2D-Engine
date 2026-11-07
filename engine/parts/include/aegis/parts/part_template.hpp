// Aegis2D — part templates & built-in registry (Milestone 7).
//
// Part templates are STATIC, shared definitions (never copied per instance).
// All content is original to Aegis2D (generic designations, no third-party
// game names, sizes or trade dress). The registry is constexpr C++ tables —
// zero external data dependencies until M9 introduces procedural assets.
#pragma once

#include <aegis/core/types.hpp>

#include <array>
#include <span>
#include <string_view>

namespace aegis::parts {

using core::f32;
using core::u8;
using core::u16;
using core::u32;
using core::i8;

/// World units per grid cell (1 cell == 1 wu — engine convention).
inline constexpr f32 kCellWu = 1.0f;

using TemplateId = u16;
using NodeId = u8;
inline constexpr TemplateId kInvalidTemplate = static_cast<TemplateId>(0xFFFF);

enum class PartCategory : u8 { CommandPod, FuelTank, Engine, Decoupler, Fairing, Strut };
enum class NodeType : u8 { Top, Bottom, Lateral, Surface };

/// Rotation encoding: low nibble = CCW steps (0..3), bit 4 = flip (mirror about
/// the local vertical axis applied BEFORE rotation). Stock SFS behaviour.
inline constexpr u8 kFlipBit = 0x10;
inline constexpr u8 rot_of(const u8 rot) noexcept { return rot & 0x0F; }
inline constexpr bool flipped(const u8 rot) noexcept { return (rot & kFlipBit) != 0; }
inline constexpr u8 make_rot(const u8 steps, const bool flip) noexcept {
    return static_cast<u8>((steps & 0x0F) | (flip ? kFlipBit : 0));
}

/// Mirror rotation across a vertical axis: R' = F · R = R(4-s) · F (flip toggles).
[[nodiscard]] inline constexpr u8 mirror_rot_vertical(const u8 rot) noexcept {
    return make_rot(static_cast<u8>((4 - rot_of(rot)) & 0x03), !flipped(rot));
}
/// Mirror across a horizontal axis: negate the y axis: R' = Fy · R, Fy = R(180)·Fx.
[[nodiscard]] inline constexpr u8 mirror_rot_horizontal(const u8 rot) noexcept {
    return make_rot(static_cast<u8>((2 + (4 - rot_of(rot))) & 0x03), !flipped(rot));
}

/// One connection node on a part template (unrotated frame, cell units).
struct NodeDef {
    NodeId id = 0;
    NodeType type = NodeType::Bottom;
    f32 x = 0.0f, y = 0.0f;   ///< node position (cell units, may lie on a boundary)
    f32 nx = 0.0f, ny = -1.0f;  ///< outward normal (unit vector, arbitrary direction)
};

struct EngineSpec { f32 thrust = 0.0f; f32 isp = 0.0f; };  ///< thrust in engine units, isp in s
struct TankSpec { f32 propellant = 0.0f; };                 ///< propellant capacity (mass units)
struct DecouplerSpec { f32 separation_delta_v = 0.0f; };     ///< applied by M8 physics

/// Static part definition. Instances are 16-byte PODs (see Part) referencing this.
struct PartTemplate {
    TemplateId id = kInvalidTemplate;
    const char* name = "";   ///< original designation, e.g. "Aegis MK-I Pod"
    PartCategory category = PartCategory::Strut;
    u8 w = 1, h = 1;          ///< footprint cells (unrotated)
    f32 dry_mass = 0.0f;
    TankSpec tank{};
    EngineSpec engine{};
    DecouplerSpec decoupler{};
    std::span<const NodeDef> nodes{};

    [[nodiscard]] bool is_engine() const noexcept { return category == PartCategory::Engine; }
    [[nodiscard]] bool is_tank() const noexcept { return category == PartCategory::FuelTank; }
    [[nodiscard]] bool is_decoupler() const noexcept { return category == PartCategory::Decoupler; }
    [[nodiscard]] bool is_fairing() const noexcept { return category == PartCategory::Fairing; }
};

/// Number of built-in templates.
inline constexpr std::size_t kTemplateCount = 13;

/// Built-in template table (order defines ids 0..kTemplateCount-1).
[[nodiscard]] const std::array<PartTemplate, kTemplateCount>& templates() noexcept;

/// Registry lookup; nullptr when id is out of range.
[[nodiscard]] const PartTemplate* find_template(TemplateId id) noexcept;

}  // namespace aegis::parts
