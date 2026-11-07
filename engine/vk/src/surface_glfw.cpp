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
        std::string message = std::string("glfwCreateWindowSurface failed: ") + vk_result_to_string(result);
        // Pull the GLFW-side error too — on Win32 it often carries the loader's
        // own HINSTANCE/hwnd detail (e.g. GLFW_API_UNAVAILABLE vs a driver error).
        const char* glfw_description = nullptr;
        if (const int glfw_error = glfwGetError(&glfw_description);
            glfw_error != GLFW_NO_ERROR && glfw_description != nullptr) {
            message += " (glfw error " + std::to_string(glfw_error) + ": " + glfw_description + ")";
        }
        if (result == VK_ERROR_EXTENSION_NOT_PRESENT) {
            // Almost always means the instance was created without the platform
            // surface extensions (VK_KHR_surface / VK_KHR_win32_surface) — e.g.
            // glfw built with the Null platform returns no required extensions.
            message += " (the Vulkan instance is missing VK_KHR_surface / VK_KHR_win32_surface; "
                       "was glfw built with the Null platform on a display machine?)";
        }
        return core::Result<GlfwSurface>::failure(core::ErrorCode::VulkanFailure, std::move(message),
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
