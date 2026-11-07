#include <aegis/renderer2d/projection.hpp>

namespace aegis::renderer2d {

std::array<core::f32, 16> make_orthographic(const core::f32 left,
                                            const core::f32 right,
                                            const core::f32 bottom,
                                            const core::f32 top,
                                            const core::f32 near_z,
                                            const core::f32 far_z) noexcept {
    const core::f32 sx = 2.0f / (right - left);
    const core::f32 sy = 2.0f / (top - bottom);
    const core::f32 sz = 1.0f / (far_z - near_z);
    const core::f32 tx = -(right + left) / (right - left);
    const core::f32 ty = -(top + bottom) / (top - bottom);
    const core::f32 tz = -near_z / (far_z - near_z);

    // Column-major layout (m[col * 4 + row]).
    return {sx,  0.0f, 0.0f, 0.0f,  // column 0
            0.0f, sy,  0.0f, 0.0f,  // column 1
            0.0f, 0.0f, sz,   0.0f,  // column 2
            tx,   ty,   tz,   1.0f}; // column 3
}

}  // namespace aegis::renderer2d
