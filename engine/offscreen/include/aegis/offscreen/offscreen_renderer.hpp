// Aegis2D — surface-free offscreen renderer (Milestone 5).
//
// Renders RGBA8 frames into a private VkImage with the shared instanced batch
// renderer, then copies the result into a persistently mapped HOST_VISIBLE
// readback buffer. No window, no surface, no swapchain — usable headlessly
// (tools, CI) as long as a Vulkan 1.3 driver is present.
//
// Threading: single-threaded, like the rest of the engine.
#pragma once

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/renderer2d/batch_renderer.hpp>

#include <array>
#include <functional>
#include <vector>

namespace aegis::offscreen {

struct OffscreenRendererConfig {
    core::u32 width = 1280;
    core::u32 height = 720;
    std::array<core::f32, 4> clear_color = {0.012f, 0.016f, 0.046f, 1.0f};  // deep space
    core::u32 max_instances = 4096;
};

class OffscreenRenderer : public core::NonCopyable {
public:
    /// Borrowed handles: `device` and `queue` must outlive the renderer.
    /// `queue` is used both for command submission and pool creation.
    [[nodiscard]] static core::Result<OffscreenRenderer> create(
        VkDevice device,
        const VkPhysicalDeviceMemoryProperties& memory_properties,
        VkQueue queue,
        core::u32 queue_family,
        const OffscreenRendererConfig& config = {});
    ~OffscreenRenderer();
    OffscreenRenderer(OffscreenRenderer&& other) noexcept;
    OffscreenRenderer& operator=(OffscreenRenderer&& other) noexcept;

    /// Renders exactly one frame into the offscreen image and reads back
    /// tightly packed RGBA8 pixels (width * height * 4 bytes).
    ///
    /// `view_projection` is the column-major mat4 fed to the batch renderer;
    /// `draw` records the frame's instances and is invoked between the
    /// renderer's begin_frame() and flush() calls.
    [[nodiscard]] core::Result<std::vector<core::u8>> capture(
        const std::array<core::f32, 16>& view_projection,
        const std::function<void(renderer2d::BatchRenderer&)>& draw);

    [[nodiscard]] core::u32 width() const noexcept { return config_.width; }
    [[nodiscard]] core::u32 height() const noexcept { return config_.height; }

private:
    OffscreenRenderer() = default;

    void destroy() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    core::u32 queue_family_ = 0;
    OffscreenRendererConfig config_{};
    VkImage image_ = VK_NULL_HANDLE;
    VkDeviceMemory image_memory_ = VK_NULL_HANDLE;
    VkImageView image_view_ = VK_NULL_HANDLE;
    VkBuffer readback_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    core::u8* readback_mapped_ = nullptr;  ///< persistent map (HOST_VISIBLE)
    bool readback_coherent_ = false;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    renderer2d::BatchRenderer batch_renderer_{};  ///< internal, bound to R8G8B8A8_UNORM
};

}  // namespace aegis::offscreen
