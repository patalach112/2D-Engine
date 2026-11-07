// Aegis2D — procedural part rendering for VAB assemblies and flight (Milestone 9).
//
// Read-only views: the renderer reads aegis_parts state and emits PartInstance
// draws. Deterministic: parts iterate in stored index order; liveries derive
// from a stable hash of (part index, template id) — identical assemblies
// produce bitwise-identical instance streams.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/parts/vehicle_assembly.hpp>
#include <aegis/renderer2d/batch_renderer.hpp>
#include <aegis/renderer2d/part_instance.hpp>

#include <array>
#include <span>

namespace aegis::render {

inline constexpr core::u32 kNoPart = 0xFFFFFFFFu;

/// Map a built-in template's category to its SDF part kind.
[[nodiscard]] aegis::renderer2d::PartKind part_kind_of(const parts::PartTemplate& tmpl) noexcept;

/// Tasteful livery palette, picked by hash(index, template). Deterministic.
[[nodiscard]] std::array<core::f32, 4> skin_tint(const parts::PartTemplate& tmpl,
                                                 core::u32 part_index) noexcept;

/// [0, 1) variant jitter for the part shader (livery/panel noise).
[[nodiscard]] core::f32 variant_of(core::u32 part_index, parts::TemplateId tmpl) noexcept;

/// Rotation (radians) applied to instances so assemblies author upright (+y)
/// and rotate about their CoM with the vehicle.
[[nodiscard]] constexpr core::f32 heading_to_rotation(const core::f64 heading) noexcept {
    return static_cast<core::f32>(heading - 1.5707963267948966);  // pi/2 == vertical == no rotation
}

struct AssemblyDrawOptions {
    core::f64 clock_s = 0.0;                 ///< drives beacon blink / plume flicker
    core::f64 throttle = 0.0;                ///< [0, 1] nozzle glow for ignited engines
    core::u32 highlight_index = kNoPart;     ///< cyan snap-target outline
    core::u32 ghost_index = kNoPart;         ///< translucent placement preview
};

/// Draws a whole assembly (alive parts only), iterating in fixed part-index
/// order. `origin` is the assembly frame origin in world metres (launch pad /
/// CoM); `heading` rotates the assembly about that origin (pi/2 = upright).
class PartRenderer {
public:
    void draw_assembly(aegis::renderer2d::BatchRenderer& renderer, const parts::VehicleAssembly& assembly,
                       core::f64 origin_x, core::f64 origin_y, core::f64 heading,
                       const AssemblyDrawOptions& options) const noexcept;

    /// Single part draw (VAB palette previews, drag ghosts).
    void draw_single(aegis::renderer2d::BatchRenderer& renderer, const parts::PartTemplate& tmpl,
                     core::u32 part_index, core::f32 world_x, core::f32 world_y, core::f32 width_wu,
                     core::f32 height_wu, core::f32 rotation, const AssemblyDrawOptions& options,
                     core::u32 flags) const noexcept;

    /// One exhaust plume per ignited engine, emitted from the engine's bottom
    /// edge along local -y. `engine_part_indices` are assembly part indices.
    void draw_plumes(aegis::renderer2d::BatchRenderer& renderer, const parts::VehicleAssembly& assembly,
                     core::f64 origin_x, core::f64 origin_y, core::f64 heading, core::f64 throttle,
                     core::f64 clock_s, std::span<const core::u32> engine_part_indices) const noexcept;
};

}  // namespace aegis::render
