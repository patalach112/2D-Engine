#include <aegis/vk/surface_glfw.hpp>

#include <aegis/core/log.hpp>
#include <aegis/vk/common.hpp>

#define GLFW_INCLUDE_NONE  // this file includes no GL headers
#include <GLFW/glfw3.h>

#include <string>
namespace aegis::vk {

GlfwSurface::~GlfwSurface() { reset(); }

GlfwSurface::GlfwSurface(GlfwSurface&& other) noexcept
    : instance_(other.instance_), surface_(other.surface_) {
    other.instance_ = VK_NULL_HANDLE;
    other.surface_ = VK_NULL_HANDLE;
}

GlfwSurface& GlfwSurface::operator=(GlfwSurface&& other) noexcept {
    if (this != &other) {
        reset();
        instance_ = other.instance_;
        surface_ = other.surface_;
        other.instance_ = VK_NULL_HANDLE;
        other.surface_ = VK_NULL_HANDLE;
    }
    return *this;
}

core::Result<GlfwSurface> GlfwSurface::create(const Instance& instance, GLFWwindow* const window) {
    if (window == nullptr) {
        return core::Result<GlfwSurface>::failure(core::ErrorCode::InvalidArgument,
                                                  "GlfwSurface::create requires a valid GLFWwindow");
    }

    GlfwSurface surface;
    surface.instance_ = instance.handle();

    VkSurfaceKHR raw = VK_NULL_HANDLE;
    if (const VkResult result = glfwCreateWindowSurface(instance.handle(), window, nullptr, &raw);
        result != VK_SUCCESS) {
        return core::Result<GlfwSurface>::failure(core::ErrorCode::VulkanFailure,
                                                  std::string("glfwCreateWindowSurface failed: ") +
                                                      vk_result_to_string(result),
                                                  static_cast<core::i32>(result));
    }
    surface.surface_ = raw;
    return core::Result<GlfwSurface>::ok(std::move(surface));
}

void GlfwSurface::reset() noexcept {
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    surface_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
}

} // namespace aegis::vk
