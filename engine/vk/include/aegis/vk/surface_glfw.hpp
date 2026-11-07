// Aegis2D — GLFW surface creation (windowed builds only).
#pragma once

#include <aegis/export.h>

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/vk/instance.hpp>

struct GLFWwindow;  // opaque C type — no export annotation  // forward declaration — this header must not require glfw3.h

namespace aegis::vk {

/// RAII wrapper owning a VkSurfaceKHR created from a GLFWwindow.
class AEGIS_API GlfwSurface : public core::NonCopyable {
public:
    GlfwSurface() = default;
    ~GlfwSurface();
    GlfwSurface(GlfwSurface&& other) noexcept;
    GlfwSurface& operator=(GlfwSurface&& other) noexcept;

    [[nodiscard]] static core::Result<GlfwSurface> create(const Instance& instance, GLFWwindow* window);

    VkSurfaceKHR handle() const noexcept { return surface_; }

private:
    void reset() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
};

} // namespace aegis::vk
