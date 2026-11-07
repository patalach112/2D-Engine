// Aegis2D — procedural planet, atmosphere and sky rendering (Milestone 9).
//
// No textures: the planet is a circle-SDF disc (quad shader, negative
// softness), the atmosphere is four stacked annulus discs with decreasing
// alpha, and the flight sky is 24 hard horizontal bands lerped between the
// zenith and horizon colours — a vertical gradient at zero shader cost.
// Everything is deterministic per frame.
#pragma once

#include <aegis/core/types.hpp>
#include <aegis/renderer2d/batch_renderer.hpp>
#include <aegis/renderer2d/camera.hpp>

#include <array>

namespace aegis::render {

/// Earth-like body parameters for the flight/map scenes. Units: metres.
struct PlanetStyle {
    core::f64 radius_m = 6.371e6;          ///< matches physics CelestialBody default
    std::array<core::f32, 4> land{0.24f, 0.38f, 0.24f, 1.0f};        ///< disc base (pasture green)
    std::array<core::f32, 4> land_shade{0.12f, 0.22f, 0.17f, 1.0f};  ///< disc lower hemisphere
    std::array<core::f32, 4> ocean_limb{0.30f, 0.52f, 0.66f, 1.0f};  ///< disc limb glow
    std::array<core::f32, 4> atmosphere{0.55f, 0.75f, 0.95f, 1.0f};  ///< annulus colour
};

/// Full-screen sky gradient for the flight view. `altitude_fraction` blends
/// from sea-level haze (0) to black space (1). `star_seed` keeps the star
/// field stable between frames. Drawn in world space (covers the viewport).
void draw_sky(aegis::renderer2d::BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
              core::f32 altitude_fraction, core::u32 star_seed) noexcept;

/// Planet disc + stacked atmosphere annuli, centred at `center` (world m).
void draw_planet(aegis::renderer2d::BatchRenderer& renderer, core::f64 center_x, core::f64 center_y,
                 const PlanetStyle& style) noexcept;

/// Terrain strip below y == 0 (low-altitude flight). Drawn in world space.
void draw_ground(aegis::renderer2d::BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera) noexcept;

/// Launch-mount silhouette at the pad (origin). Drawn in world space.
void draw_launch_mount(aegis::renderer2d::BatchRenderer& renderer) noexcept;

}  // namespace aegis::render
