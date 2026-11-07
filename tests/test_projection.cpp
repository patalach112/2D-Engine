// Aegis2D — unit tests for the orthographic projection helper (Milestone 3).
#include <aegis/renderer2d/projection.hpp>

#include "aegis_test.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

using aegis::core::f32;
using aegis::renderer2d::make_orthographic;

namespace {

void expect_near(const float actual, const float expected, const float eps, const char* expr) {
    ++::aegis::test::check_count();
    const float delta = std::fabs(actual - expected);
    if (delta > eps) {
        ::aegis::test::report_failure(__FILE__, __LINE__,
                                      std::string("expect_near failed: ") + expr +
                                          " (got " + std::to_string(actual) + ", expected " +
                                          std::to_string(expected) + ", delta " + std::to_string(delta) + ")");
    }
}

std::array<f32, 3> mul43(const std::array<f32, 16>& m, const f32 x, const f32 y, const f32 z) {
    return {m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14]};
}

}  // namespace

AEGIS_TEST(projection, "symmetric bounds map corners to clip space") {
    const auto m = make_orthographic(-8.0f, 8.0f, -4.5f, 4.5f);  // defaults: near=-1, far=1

    AEGIS_CHECK_EQ(m.size(), std::size_t{16});
    expect_near(m[0], 1.0f / 8.0f, 1e-6f, "m[0] scale x");
    expect_near(m[5], -1.0f / 4.5f, 1e-6f, "m[5] scale y (Vulkan NDC y-down)");
    expect_near(m[10], 0.5f, 1e-6f, "m[10] scale z");   // 1 / (far - near) == 1/2
    expect_near(m[12], 0.0f, 1e-6f, "m[12] translate x (symmetric)");
    expect_near(m[13], 0.0f, 1e-6f, "m[13] translate y (symmetric)");
    expect_near(m[14], 0.5f, 1e-6f, "m[14] translate z");  // -near / (far - near) == 0.5
    expect_near(m[15], 1.0f, 1e-6f, "m[15] w = 1");

    for (const std::size_t i : {std::size_t{1}, std::size_t{2}, std::size_t{3}, std::size_t{4},
                                std::size_t{6}, std::size_t{7}, std::size_t{8}, std::size_t{9}, std::size_t{11}}) {
        expect_near(m[i], 0.0f, 1e-6f, "off-diagonal must be zero");
    }
}

AEGIS_TEST(projection, "asymmetric bounds translate correctly") {
    const auto m = make_orthographic(-2.0f, 6.0f, -1.0f, 3.0f, 0.0f, 10.0f);

    expect_near(m[0], 2.0f / 8.0f, 1e-6f, "m[0] scale x");
    expect_near(m[12], -0.5f, 1e-6f, "m[12] translate x");   // -(right+left)/(right-left)
    expect_near(m[5], -0.5f, 1e-6f, "m[5] scale y (Vulkan NDC y-down)");
    expect_near(m[13], 0.5f, 1e-6f, "m[13] translate y");    // (top+bottom)/(top-bottom)
    expect_near(m[10], 0.1f, 1e-6f, "m[10] scale z");
    expect_near(m[14], 0.0f, 1e-6f, "m[14] translate z");

    const auto lo = mul43(m, -2.0f, -1.0f, 0.0f);
    expect_near(lo[0], -1.0f, 1e-5f, "left bound -> clip x = -1");
    expect_near(lo[1], 1.0f, 1e-5f, "bottom bound -> clip y = +1 (Vulkan NDC y-down)");
    expect_near(lo[2], 0.0f, 1e-5f, "near bound -> clip z = 0");

    const auto hi = mul43(m, 6.0f, 3.0f, 10.0f);
    expect_near(hi[0], 1.0f, 1e-5f, "right bound -> clip x = +1");
    expect_near(hi[1], -1.0f, 1e-5f, "top bound -> clip y = -1 (Vulkan NDC y-down)");
    expect_near(hi[2], 1.0f, 1e-5f, "far bound -> clip z = +1");
}

AEGIS_TEST(projection, "world origin maps to clip origin for symmetric bounds") {
    const auto m = make_orthographic(-8.0f, 8.0f, -4.5f, 4.5f);
    const auto o = mul43(m, 0.0f, 0.0f, 0.0f);
    expect_near(o[0], 0.0f, 1e-6f, "origin x");
    expect_near(o[1], 0.0f, 1e-6f, "origin y");
    expect_near(o[2], 0.5f, 1e-6f, "origin z (near=-1, far=1 -> 0.5)");
}
