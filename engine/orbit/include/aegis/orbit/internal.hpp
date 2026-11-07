// Aegis2D — INTERNAL orbital-mechanics kernels (C++ fallback).
//
// Unstable surface: for the aegis_orbit implementation itself and for the
// A/B equivalence tests (tests/test_orbit_kernels.cpp). Do NOT call from
// engine or game code — use aegis::orbit::solve_kepler / state_at /
// sample_path, which dispatch to the Fortran kernels when available.
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>
#include <aegis/orbit/orbit.hpp>

#include <array>
#include <vector>

namespace aegis::orbit::detail {

[[nodiscard]] AEGIS_API core::f64 fallback_solve_kepler(core::f64 mean_anomaly, core::f64 eccentricity) noexcept;
[[nodiscard]] AEGIS_API State fallback_state_at(const Elements& elements, core::f64 t) noexcept;
[[nodiscard]] AEGIS_API std::vector<std::array<core::f32, 2>> fallback_sample_path(const Elements& elements,
                                                                         core::u32 samples);

}  // namespace aegis::orbit::detail
