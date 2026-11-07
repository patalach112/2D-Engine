// Aegis2D — unit tests for Result<T> (Milestone 1).
#include <aegis/core/result.hpp>

#include "aegis_test.hpp"

using aegis::core::ErrorCode;

AEGIS_TEST(result, "ok(T) carries a payload") {
    auto value = aegis::core::Result<int>::ok(42);
    AEGIS_REQUIRE(value.has_value());
    AEGIS_CHECK_EQ(value.value(), 42);
    AEGIS_CHECK_EQ(std::move(value).take(), 42);
}

AEGIS_TEST(result, "failure carries code, message and native code") {
    auto failure = aegis::core::Result<int>::failure(ErrorCode::VulkanFailure, "device lost", -4);
    AEGIS_REQUIRE(!failure.has_value());
    AEGIS_CHECK(failure.error().code == ErrorCode::VulkanFailure);
    AEGIS_CHECK_EQ(failure.error().native_code, -4);
    AEGIS_CHECK_EQ(failure.error().message, std::string("device lost"));
    AEGIS_CHECK(failure.error().to_string().find("VulkanFailure") != std::string::npos);
    AEGIS_CHECK(failure.error().to_string().find("device lost") != std::string::npos);
}

AEGIS_TEST(result, "error_code_name covers every enumerator") {
    const std::pair<ErrorCode, const char*> expected[] = {
        {ErrorCode::Unknown, "Unknown"},
        {ErrorCode::InvalidArgument, "InvalidArgument"},
        {ErrorCode::NotFound, "NotFound"},
        {ErrorCode::AlreadyExists, "AlreadyExists"},
        {ErrorCode::Unavailable, "Unavailable"},
        {ErrorCode::OutOfMemory, "OutOfMemory"},
        {ErrorCode::IoError, "IoError"},
        {ErrorCode::VulkanFailure, "VulkanFailure"},
    };
    for (const auto& [code, name] : expected) {
        AEGIS_CHECK_EQ(std::string(aegis::core::error_code_name(code)), std::string(name));
    }
}

AEGIS_TEST(result, "Result<void> ok/failure round trip") {
    auto ok = aegis::core::Result<void>::ok();
    AEGIS_CHECK(ok.has_value());

    auto failure = aegis::core::Result<void>::failure(ErrorCode::InvalidArgument, "bad arg");
    AEGIS_REQUIRE(!failure.has_value());
    AEGIS_CHECK(failure.error().code == ErrorCode::InvalidArgument);
    AEGIS_CHECK_EQ(failure.error().message, std::string("bad arg"));
}

AEGIS_TEST(result, "failure() overloads produce native-code-zero errors") {
    auto failure = aegis::core::Result<void>::failure(ErrorCode::IoError, "io failed");
    AEGIS_REQUIRE(!failure.has_value());
    AEGIS_CHECK_EQ(failure.error().native_code, 0);
}
