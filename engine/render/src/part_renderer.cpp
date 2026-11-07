// Aegis2D — procedural part instance builder (Milestone 9).
#include <aegis/render/part_renderer.hpp>

#include <aegis/parts/part_template.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string_view>

using aegis::renderer2d::PartInstance;
using aegis::renderer2d::PartKind;

namespace aegis::render {
namespace {

constexpr core::u32 kHashOffset = 2166136261u;
constexpr core::u32 kHashPrime = 16777619u;

core::u32 hash_bytes(core::u32 seed, const void* data, const std::size_t size) noexcept {
    core::u32 hash = seed;
    const auto* bytes = static_cast<const core::u8*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kHashPrime;
    }
    return hash;
}

core::u32 hash2(const core::u32 a, const core::u32 b) noexcept {
    const core::u32 h1 = hash_bytes(kHashOffset, &a, sizeof(a));
    return hash_bytes(h1 ^ 0x9e3779b9u, &b, sizeof(b));
}

core::f32 hash01(const core::u32 a, const core::u32 b) noexcept {
    return static_cast<core::f32>(hash2(a, b) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

}  // namespace

PartKind part_kind_of(const parts::PartTemplate& tmpl) noexcept {
    using parts::PartCategory;
    switch (tmpl.category) {
        case PartCategory::CommandPod:
            return PartKind::Pod;
        case PartCategory::FuelTank:
            return PartKind::FuelTank;
        case PartCategory::Engine:
            return PartKind::Engine;
        case PartCategory::Decoupler:
            return std::string_view{tmpl.name}.find("Separator") != std::string_view::npos
                       ? PartKind::Separator
                       : PartKind::Decoupler;
        case PartCategory::Fairing:
            return PartKind::Fairing;
        case PartCategory::Strut:
            return PartKind::Strut;
    }
    return PartKind::FuelTank;
}

std::array<core::f32, 4> skin_tint(const parts::PartTemplate& tmpl, const core::u32 part_index) noexcept {
    // Tasteful livery palettes per category; index picked by stable hash.
    const core::u32 pick = hash2(part_index, static_cast<core::u32>(tmpl.id) + 1u);
    const core::f32 jitter = 0.90f + 0.20f * hash01(part_index, 0xBEEFu);
    auto tint = [jitter](const std::array<core::f32, 3> base, const core::f32 alpha = 1.0f) {
        return std::array<core::f32, 4>{base[0] * jitter, base[1] * jitter, base[2] * jitter, alpha};
    };
    using parts::PartCategory;
    switch (tmpl.category) {
        case PartCategory::CommandPod: {
            constexpr std::array<std::array<core::f32, 3>, 3> palette{
                {{0.90f, 0.88f, 0.83f}, {0.76f, 0.78f, 0.84f}, {0.88f, 0.78f, 0.66f}}};
            return tint(palette[pick % palette.size()]);
        }
        case PartCategory::FuelTank: {
            constexpr std::array<std::array<core::f32, 3>, 4> palette{
                {{0.88f, 0.85f, 0.79f}, {0.64f, 0.68f, 0.58f}, {0.74f, 0.76f, 0.80f}, {0.87f, 0.74f, 0.58f}}};
            return tint(palette[pick % palette.size()]);
        }
        case PartCategory::Engine:
            return tint({0.34f, 0.34f, 0.38f});
        case PartCategory::Decoupler:
            return tint({0.55f, 0.55f, 0.60f});
        case PartCategory::Fairing: {
            constexpr std::array<std::array<core::f32, 3>, 2> palette{{{0.88f, 0.86f, 0.82f}, {0.80f, 0.84f, 0.90f}}};
            return tint(palette[pick % palette.size()]);
        }
        case PartCategory::Strut:
            return tint({0.44f, 0.45f, 0.50f});
    }
    return std::array<core::f32, 4>{0.8f, 0.8f, 0.8f, 1.0f};
}

core::f32 variant_of(const core::u32 part_index, const parts::TemplateId tmpl) noexcept {
    return hash01(part_index, static_cast<core::u32>(tmpl) + 0x51FAu);
}

void PartRenderer::draw_assembly(aegis::renderer2d::BatchRenderer& renderer,
                                 const parts::VehicleAssembly& assembly, const core::f64 origin_x,
                                 const core::f64 origin_y, const core::f64 heading,
                                 const AssemblyDrawOptions& options) const noexcept {
    const core::f64 rot = heading - 1.5707963267948966;
    const core::f64 c = std::cos(rot);
    const core::f64 s = std::sin(rot);
    const core::f32 clock_s = static_cast<core::f32>(options.clock_s);
    const core::f32 throttle = static_cast<core::f32>(options.throttle);

    const auto& part_slots = assembly.parts();
    for (core::u32 index = 0; index < static_cast<core::u32>(part_slots.size()); ++index) {
        const parts::Part* part = assembly.part(index);
        if (part == nullptr) {
            continue;  // tombstoned slot — keeps iteration order fixed
        }
        const parts::PartTemplate* tmpl = assembly.template_of(index);
        if (tmpl == nullptr) {
            continue;
        }
        const auto center = assembly.part_center_wu(index);  // metres, assembly frame, y-up
        const core::f64 wx = origin_x + static_cast<core::f64>(center[0]) * c - static_cast<core::f64>(center[1]) * s;
        const core::f64 wy = origin_y + static_cast<core::f64>(center[0]) * s + static_cast<core::f64>(center[1]) * c;

        const auto [rotated_w, rotated_h] = parts::rotated_dims(tmpl->w, tmpl->h, part->rot);
        core::u32 flags = 0;
        if (index == options.ghost_index) {
            flags |= static_cast<core::u32>(aegis::renderer2d::kPartFlagGhost);
        }
        if (index == options.highlight_index) {
            flags |= static_cast<core::u32>(aegis::renderer2d::kPartFlagHighlight);
        }
        if (parts::flipped(part->rot)) {
            flags |= static_cast<core::u32>(aegis::renderer2d::kPartFlagFlip);
        }

        PartInstance instance{};
        instance.x = static_cast<core::f32>(wx);
        instance.y = static_cast<core::f32>(wy);
        instance.width = static_cast<core::f32>(rotated_w);
        instance.height = static_cast<core::f32>(rotated_h);
        instance.rotation =
            static_cast<core::f32>(rot) + static_cast<core::f32>(parts::rot_of(part->rot)) * 1.5707963267948966f;
        const auto tint = skin_tint(*tmpl, index);
        instance.r = tint[0];
        instance.g = tint[1];
        instance.b = tint[2];
        instance.a = tint[3];
        instance.kind = static_cast<core::f32>(static_cast<core::u32>(part_kind_of(*tmpl)));
        instance.variant = variant_of(index, tmpl->id);
        instance.clock = clock_s;
        instance.throttle = part_kind_of(*tmpl) == PartKind::Engine ? throttle : 0.0f;
        instance.flags = static_cast<core::f32>(flags);
        renderer.draw_part(instance);
    }
}

void PartRenderer::draw_single(aegis::renderer2d::BatchRenderer& renderer, const parts::PartTemplate& tmpl,
                               const core::u32 part_index, const core::f32 world_x, const core::f32 world_y,
                               const core::f32 width_wu, const core::f32 height_wu, const core::f32 rotation,
                               const AssemblyDrawOptions& options, const core::u32 flags) const noexcept {
    PartInstance instance{};
    instance.x = world_x;
    instance.y = world_y;
    instance.width = width_wu;
    instance.height = height_wu;
    instance.rotation = rotation;
    const auto tint = skin_tint(tmpl, part_index);
    instance.r = tint[0];
    instance.g = tint[1];
    instance.b = tint[2];
    instance.a = tint[3];
    instance.kind = static_cast<core::f32>(static_cast<core::u32>(part_kind_of(tmpl)));
    instance.variant = variant_of(part_index, tmpl.id);
    instance.clock = static_cast<core::f32>(options.clock_s);
    instance.throttle = 0.0f;
    instance.flags = static_cast<core::f32>(flags);
    renderer.draw_part(instance);
}

void PartRenderer::draw_plumes(aegis::renderer2d::BatchRenderer& renderer,
                               const parts::VehicleAssembly& assembly, const core::f64 origin_x,
                               const core::f64 origin_y, const core::f64 heading, const core::f64 throttle,
                               const core::f64 clock_s, const std::span<const core::u32> engine_part_indices) const noexcept {
    if (throttle <= 0.0) {
        return;  // no glow, no plume — deterministic silence
    }
    const core::f64 rot = heading - 1.5707963267948966;
    const core::f64 c = std::cos(rot);
    const core::f64 s = std::sin(rot);
    for (const core::u32 index : engine_part_indices) {
        const parts::Part* part = assembly.part(index);
        const parts::PartTemplate* tmpl = assembly.template_of(index);
        if (part == nullptr || tmpl == nullptr) {
            continue;
        }
        const auto center = assembly.part_center_wu(index);
        const auto [rotated_w, rotated_h] = parts::rotated_dims(tmpl->w, tmpl->h, part->rot);
        // Nozzle tip: bottom edge of the engine in assembly frame.
        const core::f64 lx = static_cast<core::f64>(center[0]);
        const core::f64 bottom = static_cast<core::f64>(center[1]) - static_cast<core::f64>(rotated_h) * 0.5;
        // Plume: cone pointing down, length pulses with throttle + clock flicker.
        const core::f64 flicker = 1.0 + 0.09 * std::sin(clock_s * 41.0 + static_cast<core::f64>(index) * 1.7);
        const core::f64 length = (1.6 + 3.4 * throttle) * static_cast<core::f64>(rotated_h) * flicker;
        const core::f64 width = static_cast<core::f64>(rotated_w) * (0.55 + 0.25 * throttle);
        const core::f64 mid_lx = lx;
        const core::f64 mid_ly = bottom - length * 0.5;
        const core::f64 wx = origin_x + mid_lx * c - mid_ly * s;
        const core::f64 wy = origin_y + mid_lx * s + mid_ly * c;

        PartInstance instance{};
        instance.x = static_cast<core::f32>(wx);
        instance.y = static_cast<core::f32>(wy);
        instance.width = static_cast<core::f32>(width);
        instance.height = static_cast<core::f32>(length);
        instance.rotation = static_cast<core::f32>(rot);
        instance.r = 1.0f;
        instance.g = 1.0f;
        instance.b = 1.0f;
        instance.a = 1.0f;
        instance.kind = static_cast<core::f32>(static_cast<core::u32>(PartKind::Plume));
        instance.variant = variant_of(index, tmpl->id);
        instance.clock = static_cast<core::f32>(clock_s);
        instance.throttle = static_cast<core::f32>(throttle);
        instance.flags = 0.0f;
        renderer.draw_part(instance);
    }
}

}  // namespace aegis::render
