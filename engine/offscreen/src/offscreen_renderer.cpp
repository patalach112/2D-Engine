#include <aegis/offscreen/offscreen_renderer.hpp>

#include <aegis/core/log.hpp>
#include <aegis/memory/memory_props.hpp>

#include <cstring>
#include <vector>

namespace aegis::offscreen {

using core::f32;
using core::u8;
using core::u32;
using core::u64;

OffscreenRenderer::~OffscreenRenderer() { destroy(); }

OffscreenRenderer::OffscreenRenderer(OffscreenRenderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      queue_(std::exchange(other.queue_, VK_NULL_HANDLE)),
      queue_family_(other.queue_family_),
      config_(other.config_),
      image_(std::exchange(other.image_, VK_NULL_HANDLE)),
      image_memory_(std::exchange(other.image_memory_, VK_NULL_HANDLE)),
      image_view_(std::exchange(other.image_view_, VK_NULL_HANDLE)),
      readback_(std::exchange(other.readback_, VK_NULL_HANDLE)),
      readback_memory_(std::exchange(other.readback_memory_, VK_NULL_HANDLE)),
      readback_mapped_(std::exchange(other.readback_mapped_, nullptr)),
      readback_coherent_(other.readback_coherent_),
      pool_(std::exchange(other.pool_, VK_NULL_HANDLE)),
      command_buffer_(std::exchange(other.command_buffer_, VK_NULL_HANDLE)),
      fence_(std::exchange(other.fence_, VK_NULL_HANDLE)),
      batch_renderer_(std::move(other.batch_renderer_)) {
    other.queue_family_ = 0;
    other.config_ = OffscreenRendererConfig{};
    other.readback_coherent_ = false;
}

OffscreenRenderer& OffscreenRenderer::operator=(OffscreenRenderer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        queue_ = std::exchange(other.queue_, VK_NULL_HANDLE);
        queue_family_ = other.queue_family_;
        config_ = other.config_;
        image_ = std::exchange(other.image_, VK_NULL_HANDLE);
        image_memory_ = std::exchange(other.image_memory_, VK_NULL_HANDLE);
        image_view_ = std::exchange(other.image_view_, VK_NULL_HANDLE);
        readback_ = std::exchange(other.readback_, VK_NULL_HANDLE);
        readback_memory_ = std::exchange(other.readback_memory_, VK_NULL_HANDLE);
        readback_mapped_ = std::exchange(other.readback_mapped_, nullptr);
        readback_coherent_ = other.readback_coherent_;
        pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
        command_buffer_ = std::exchange(other.command_buffer_, VK_NULL_HANDLE);
        fence_ = std::exchange(other.fence_, VK_NULL_HANDLE);
        batch_renderer_ = std::move(other.batch_renderer_);
        other.queue_family_ = 0;
        other.config_ = OffscreenRendererConfig{};
        other.readback_coherent_ = false;
    }
    return *this;
}

namespace {

core::Result<void> create_image_and_view_impl(const VkDevice device,
                                         const VkPhysicalDeviceMemoryProperties& memory_properties,
                                         const u32 width, const u32 height,
                                         VkImage& out_image, VkDeviceMemory& out_memory,
                                         VkImageView& out_view) {
    const VkImageCreateInfo image_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    if (vkCreateImage(device, &image_info, nullptr, &out_image) != VK_SUCCESS) {
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure, "vkCreateImage failed");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, out_image, &requirements);
    const u32 memory_type = memory::find_memory_type(memory_properties, requirements.memoryTypeBits,
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memory_type == ~u32{0}) {
        vkDestroyImage(device, out_image, nullptr);
        out_image = VK_NULL_HANDLE;
        return core::Result<void>::failure(core::ErrorCode::Unavailable,
                                           "no DEVICE_LOCAL memory type for the offscreen image");
    }
    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (vkAllocateMemory(device, &allocate_info, nullptr, &out_memory) != VK_SUCCESS) {
        vkDestroyImage(device, out_image, nullptr);
        out_image = VK_NULL_HANDLE;
        return core::Result<void>::failure(core::ErrorCode::OutOfMemory, "vkAllocateMemory failed");
    }
    vkBindImageMemory(device, out_image, out_memory, 0);

    const VkImageViewCreateInfo view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .image = out_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .components = {},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    if (vkCreateImageView(device, &view_info, nullptr, &out_view) != VK_SUCCESS) {
        vkDestroyImage(device, out_image, nullptr);
        vkFreeMemory(device, out_memory, nullptr);
        out_image = VK_NULL_HANDLE;
        out_memory = VK_NULL_HANDLE;
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure, "vkCreateImageView failed");
    }
    return core::Result<void>::ok();
}

}  // namespace

core::Result<OffscreenRenderer> OffscreenRenderer::create(
    const VkDevice device,
    const VkPhysicalDeviceMemoryProperties& memory_properties,
    const VkQueue queue,
    const core::u32 queue_family,
    const OffscreenRendererConfig& config) {
    if (device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE) {
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::InvalidArgument,
                                                        "OffscreenRenderer requires a device and a queue");
    }
    if (config.width == 0 || config.height == 0) {
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::InvalidArgument,
                                                        "offscreen dimensions must be positive");
    }

    OffscreenRenderer renderer;
    renderer.device_ = device;
    renderer.queue_ = queue;
    renderer.queue_family_ = queue_family;
    renderer.config_ = config;

    if (auto result = create_image_and_view_impl(device, memory_properties, config.width, config.height,
                                            renderer.image_, renderer.image_memory_,
                                            renderer.image_view_);
        !result) {
        return core::Result<OffscreenRenderer>::failure(result.error().code, result.error().message,
                                                        result.error().native_code);
    }

    // --- readback buffer (persistently mapped) --------------------------------
    const u64 readback_bytes = static_cast<u64>(config.width) * config.height * 4u;
    const VkBufferCreateInfo buffer_info{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = readback_bytes,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    if (vkCreateBuffer(device, &buffer_info, nullptr, &renderer.readback_) != VK_SUCCESS) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::VulkanFailure,
                                                        "vkCreateBuffer failed");
    }
    VkMemoryRequirements buffer_requirements{};
    vkGetBufferMemoryRequirements(device, renderer.readback_, &buffer_requirements);
    u32 memory_type = memory::find_memory_type(
        memory_properties, buffer_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    renderer.readback_coherent_ = memory_type != ~u32{0};
    if (!renderer.readback_coherent_) {
        memory_type = memory::find_memory_type(memory_properties, buffer_requirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    }
    if (memory_type == ~u32{0}) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::Unavailable,
                                                        "no HOST_VISIBLE memory type for the readback buffer");
    }
    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = buffer_requirements.size,
        .memoryTypeIndex = memory_type,
    };
    if (vkAllocateMemory(device, &allocate_info, nullptr, &renderer.readback_memory_) != VK_SUCCESS) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::OutOfMemory,
                                                        "vkAllocateMemory failed");
    }
    vkBindBufferMemory(device, renderer.readback_, renderer.readback_memory_, 0);
    if (vkMapMemory(device, renderer.readback_memory_, 0, buffer_requirements.size, 0,
                    reinterpret_cast<void**>(&renderer.readback_mapped_)) != VK_SUCCESS) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::VulkanFailure,
                                                        "vkMapMemory failed");
    }

    // --- command pool / buffer / fence -----------------------------------------
    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queue_family,
    };
    if (vkCreateCommandPool(device, &pool_info, nullptr, &renderer.pool_) != VK_SUCCESS) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::VulkanFailure,
                                                        "vkCreateCommandPool failed");
    }
    const VkCommandBufferAllocateInfo allocate_cmd{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = renderer.pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device, &allocate_cmd, &renderer.command_buffer_) != VK_SUCCESS) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::VulkanFailure,
                                                        "vkAllocateCommandBuffers failed");
    }
    const VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    if (vkCreateFence(device, &fence_info, nullptr, &renderer.fence_) != VK_SUCCESS) {
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(core::ErrorCode::VulkanFailure,
                                                        "vkCreateFence failed");
    }

    // --- internal batch renderer (bound to the offscreen format) ---------------
    renderer2d::BatchRendererConfig batch_config;
    batch_config.max_instances = config.max_instances;
    batch_config.max_frames_in_flight = 1;
    auto batch = renderer2d::BatchRenderer::create(device, memory_properties,
                                                   VK_FORMAT_R8G8B8A8_UNORM, batch_config);
    if (!batch) {
        const core::ErrorCode code = batch.error().code;
        const core::i32 native = batch.error().native_code;
        const std::string message = "offscreen batch renderer: " + batch.error().message;
        renderer.destroy();
        return core::Result<OffscreenRenderer>::failure(code, message, native);
    }
    renderer.batch_renderer_ = std::move(batch).take();

    AEGIS_LOG_INFO("[aegis] offscreen renderer created (" << config.width << "x" << config.height
                   << ", RGBA8)");
    return core::Result<OffscreenRenderer>::ok(std::move(renderer));
}

core::Result<std::vector<u8>> OffscreenRenderer::capture(
    const std::array<f32, 16>& view_projection,
    const std::function<void(renderer2d::BatchRenderer&)>& draw) {
    if (!draw) {
        return core::Result<std::vector<u8>>::failure(core::ErrorCode::InvalidArgument,
                                                      "capture requires a draw callable");
    }

    const VkExtent2D extent{config_.width, config_.height};

    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);  // previous capture done
    vkResetFences(device_, 1, &fence_);
    if (vkResetCommandBuffer(command_buffer_, 0) != VK_SUCCESS) {
        return core::Result<std::vector<u8>>::failure(core::ErrorCode::VulkanFailure,
                                                      "vkResetCommandBuffer failed");
    }

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (vkBeginCommandBuffer(command_buffer_, &begin_info) != VK_SUCCESS) {
        return core::Result<std::vector<u8>>::failure(core::ErrorCode::VulkanFailure,
                                                      "vkBeginCommandBuffer failed");
    }

    VkImageMemoryBarrier2 to_attachment{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDependencyInfo before_barrier{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_attachment,
    };
    vkCmdPipelineBarrier2(command_buffer_, &before_barrier);

    const VkRenderingAttachmentInfo color_attachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = nullptr,
        .imageView = image_view_,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {config_.clear_color[0], config_.clear_color[1],
                                             config_.clear_color[2], config_.clear_color[3]}}},
    };
    const VkRenderingInfo rendering_info{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .pNext = nullptr,
        .flags = 0,
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .viewMask = 0,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_attachment,
        .pDepthAttachment = nullptr,
        .pStencilAttachment = nullptr,
    };
    vkCmdBeginRendering(command_buffer_, &rendering_info);
    {
        batch_renderer_.begin_frame(command_buffer_, /*frame_index=*/0, view_projection.data(), extent);
        draw(batch_renderer_);
        batch_renderer_.flush();
    }
    vkCmdEndRendering(command_buffer_);

    VkImageMemoryBarrier2 to_transfer{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = nullptr,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image_,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDependencyInfo after_barrier{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &to_transfer,
    };
    vkCmdPipelineBarrier2(command_buffer_, &after_barrier);

    const VkBufferImageCopy copy_region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageOffset = {0, 0, 0},
        .imageExtent = {config_.width, config_.height, 1},
    };
    vkCmdCopyImageToBuffer(command_buffer_, image_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_,
                           1, &copy_region);

    if (vkEndCommandBuffer(command_buffer_) != VK_SUCCESS) {
        return core::Result<std::vector<u8>>::failure(core::ErrorCode::VulkanFailure,
                                                      "vkEndCommandBuffer failed");
    }

    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = &wait_stage,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer_,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    if (vkQueueSubmit(queue_, 1, &submit_info, fence_) != VK_SUCCESS) {
        return core::Result<std::vector<u8>>::failure(core::ErrorCode::VulkanFailure,
                                                      "vkQueueSubmit failed");
    }
    vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);

    const u64 readback_bytes = static_cast<u64>(config_.width) * config_.height * 4u;
    if (!readback_coherent_) {
        const VkMappedMemoryRange range{
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = readback_memory_,
            .offset = 0,
            .size = readback_bytes,
        };
        vkInvalidateMappedMemoryRanges(device_, 1, &range);
    }

    std::vector<u8> pixels(readback_bytes);
    std::memcpy(pixels.data(), readback_mapped_, readback_bytes);
    return core::Result<std::vector<u8>>::ok(std::move(pixels));
}

void OffscreenRenderer::destroy() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        batch_renderer_ = renderer2d::BatchRenderer{};  // destroy internal pipelines/buffers first
        if (fence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, fence_, nullptr);
            fence_ = VK_NULL_HANDLE;
        }
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, pool_, nullptr);  // frees the command buffer
            pool_ = VK_NULL_HANDLE;
            command_buffer_ = VK_NULL_HANDLE;
        }
        if (readback_mapped_ != nullptr) {
            vkUnmapMemory(device_, readback_memory_);
            readback_mapped_ = nullptr;
        }
        if (readback_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, readback_, nullptr);
            readback_ = VK_NULL_HANDLE;
        }
        if (readback_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, readback_memory_, nullptr);
            readback_memory_ = VK_NULL_HANDLE;
        }
        if (image_view_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, image_view_, nullptr);
            image_view_ = VK_NULL_HANDLE;
        }
        if (image_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, image_, nullptr);
            image_ = VK_NULL_HANDLE;
        }
        if (image_memory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, image_memory_, nullptr);
            image_memory_ = VK_NULL_HANDLE;
        }
    }
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
}

}  // namespace aegis::offscreen
