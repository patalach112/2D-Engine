// Aegis2D — input snapshot for the UI kernel (Milestone 9).
//
// Plain value type: the platform layer (GLFW in apps, synthetic input in
// tests) fills one of these per frame. y coordinates are pixels, origin
// top-left, y down (screen convention).
#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

namespace aegis::ui {

enum class UiKey : core::u8 {
    None = 0,
    Space,
    Escape,
    Enter,
    Backspace,
    Delete,
    Tab,
    A, D, E, F, G, M, Q, R, S, W, X, Z,
    Up, Down, Left, Right,
    LeftShift,
    LeftCtrl,
    Num1,
    Num2,
    Num3,
    Count  // sentinel — number of key slots
};

[[nodiscard]] constexpr core::u64 key_bit(const UiKey key) noexcept {
    return key == UiKey::None ? core::u64{0} : (core::u64{1} << (static_cast<core::u64>(key) - 1u));
}

struct AEGIS_API UiInput {
    core::f64 dt = 0.0;            ///< seconds since the previous frame
    core::f32 mouse_x = 0.0f;      ///< pixels, top-left origin, y down
    core::f32 mouse_y = 0.0f;
    core::f32 wheel = 0.0f;        ///< scroll delta, + == scrolled up
    bool mouse_down = false;       ///< left button held this frame
    bool mouse_pressed = false;    ///< edge: left button went down this frame
    bool mouse_released = false;   ///< edge: left button went up this frame
    core::u64 keys_down = 0;       ///< bitmask over UiKey (see key_bit)
    core::u64 keys_pressed = 0;    ///< edge bitmask (down this frame, not last)

    [[nodiscard]] bool down(const UiKey key) const noexcept { return (keys_down & key_bit(key)) != 0; }
    [[nodiscard]] bool pressed(const UiKey key) const noexcept { return (keys_pressed & key_bit(key)) != 0; }
};

}  // namespace aegis::ui
