// Aegis2D — unit tests for the vk_common helpers (Milestone 1).
#include <aegis/vk/common.hpp>

#include "aegis_test.hpp"

#include <string>

AEGIS_TEST(vk_common, "well-known VkResult values map to stable names") {
    AEGIS_CHECK_EQ(std::string(aegis::vk::vk_result_to_string(VK_SUCCESS)), std::string("VK_SUCCESS"));
    AEGIS_CHECK_EQ(std::string(aegis::vk::vk_result_to_string(VK_NOT_READY)), std::string("VK_NOT_READY"));
    AEGIS_CHECK_EQ(std::string(aegis::vk::vk_result_to_string(VK_TIMEOUT)), std::string("VK_TIMEOUT"));
    AEGIS_CHECK_EQ(std::string(aegis::vk::vk_result_to_string(VK_ERROR_DEVICE_LOST)),
                   std::string("VK_ERROR_DEVICE_LOST"));
    AEGIS_CHECK_EQ(std::string(aegis::vk::vk_result_to_string(VK_ERROR_OUT_OF_DATE_KHR)),
                   std::string("VK_ERROR_OUT_OF_DATE_KHR"));
    AEGIS_CHECK_EQ(std::string(aegis::vk::vk_result_to_string(VK_ERROR_EXTENSION_NOT_PRESENT)),
                   std::string("VK_ERROR_EXTENSION_NOT_PRESENT"));
}

AEGIS_TEST(vk_common, "unknown VkResult reports placeholder") {
    const std::string name = aegis::vk::vk_result_to_string(static_cast<VkResult>(424242));
    AEGIS_CHECK_EQ(name, std::string("VK_RESULT_UNKNOWN"));
}

AEGIS_TEST(vk_common, "vk_check passes through VK_SUCCESS and wraps failures") {
    auto ok = aegis::vk::vk_check(VK_SUCCESS, "unit test");
    AEGIS_CHECK(ok.has_value());

    auto failure = aegis::vk::vk_check(VK_ERROR_DEVICE_LOST, "graphics queue submit");
    AEGIS_REQUIRE(!failure.has_value());
    AEGIS_CHECK(failure.error().code == aegis::core::ErrorCode::VulkanFailure);
    AEGIS_CHECK_EQ(failure.error().native_code, static_cast<aegis::core::i32>(VK_ERROR_DEVICE_LOST));
    AEGIS_CHECK(failure.error().message.find("graphics queue submit") != std::string::npos);
    AEGIS_CHECK(failure.error().message.find("VK_ERROR_DEVICE_LOST") != std::string::npos);
}
