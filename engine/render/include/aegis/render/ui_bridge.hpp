// Aegis2D — HUD draw-list -> renderer bridge (Milestone 9).
//
// The aegis_ui kernel emits a painter-ordered UiDrawItem list in *screen
// pixels* (top-left origin). This bridge re-anchors that list to world space
// every frame from the camera's current pan/zoom, so HUD elements stay glued
// to the window while the world flies behind them. Call after world
// rendering; order within the list is preserved (later == on top).
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

namespace aegis::ui {
class UiContext;
}

namespace aegis::renderer2d {
class BatchRenderer;
class Camera2D;
}

namespace aegis::render {

/// Re-emit `context.items()` as world-space glyph/UI draws anchored to the
/// camera. Deterministic; allocates nothing.
AEGIS_API void submit_ui(aegis::renderer2d::BatchRenderer& renderer, const aegis::renderer2d::Camera2D& camera,
               const aegis::ui::UiContext& context) noexcept;

}  // namespace aegis::render
