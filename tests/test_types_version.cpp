// Aegis2D — unit tests for fixed-width types and the version header (Milestone 1).
#include <aegis/core/types.hpp>
#include <aegis/core/version.hpp>

#include <type_traits>

#include "aegis_test.hpp"

using namespace aegis::core;

AEGIS_TEST(types, "aliases preserve signedness and width") {
    AEGIS_CHECK_EQ(sizeof(i8), std::size_t{1});
    AEGIS_CHECK_EQ(sizeof(i16), std::size_t{2});
    AEGIS_CHECK_EQ(sizeof(i32), std::size_t{4});
    AEGIS_CHECK_EQ(sizeof(i64), std::size_t{8});
    AEGIS_CHECK_EQ(sizeof(u8), std::size_t{1});
    AEGIS_CHECK_EQ(sizeof(u16), std::size_t{2});
    AEGIS_CHECK_EQ(sizeof(u32), std::size_t{4});
    AEGIS_CHECK_EQ(sizeof(u64), std::size_t{8});
    AEGIS_CHECK_EQ(sizeof(f32), std::size_t{4});
    AEGIS_CHECK_EQ(sizeof(f64), std::size_t{8});

    AEGIS_CHECK(std::is_signed_v<i32>);
    AEGIS_CHECK(!std::is_signed_v<u32>);
    AEGIS_CHECK(std::is_floating_point_v<f32>);
    AEGIS_CHECK(std::is_floating_point_v<f64>);
}

AEGIS_TEST(version, "milestone 8 ships 0.1.0") {
    AEGIS_CHECK_EQ(kVersionMajor, 0);
    AEGIS_CHECK_EQ(kVersionMinor, 1);
    AEGIS_CHECK_EQ(kVersionPatch, 0);
    AEGIS_CHECK_EQ(std::string(kVersionString), std::string("0.1.0"));
    AEGIS_CHECK_EQ(kMilestone, 9);
}
