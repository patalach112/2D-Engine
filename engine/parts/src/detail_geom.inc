// Internal geometry helpers for the parts module (rotation in 90-degree steps).
// Included from vehicle_assembly.cpp; not a public header.
namespace aegis::parts {
namespace detail {

struct Vec2 {
    core::f32 x, y;
};

constexpr Vec2 rotate90_ccw(const Vec2 v) noexcept { return {-v.y, v.x}; }

/// Rotate (and optionally flip) a vector in the unrotated frame into the
/// rotated frame. steps = rot_of(rot), flip first (mirror x), then rotate.
constexpr Vec2 rotate_vec(Vec2 v, const u8 rot) noexcept {
    if (flipped(rot)) {
        v.x = -v.x;
    }
    for (u8 i = 0; i < rot_of(rot); ++i) {
        v = rotate90_ccw(v);
    }
    return v;
}

}  // namespace detail
}  // namespace aegis::parts
