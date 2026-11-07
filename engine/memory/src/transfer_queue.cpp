#include <aegis/memory/transfer_queue.hpp>

#include <aegis/core/log.hpp>
#include <aegis/memory/gpu_buffer.hpp>

#include <cstring>
#include <utility>
#include <vector>

namespace aegis::memory {

using core::u64;

TransferQueue::~TransferQueue() { reset(); }

TransferQueue::TransferQueue(TransferQueue&& other) noexcept
    : device_(other.device_),
      memory_properties_(other.memory_properties_),
      pool_(other.pool_),
      queue_(other.queue_),
      family_(other.family_),
      using_dedicated_(other.using_dedicated_) {
    other.device_ = VK_NULL_HANDLE;
    other.pool_ = VK_NULL_HANDLE;
    other.queue_ = VK_NULL_HANDLE;
    other.family_ = vk::kInvalidQueueFamily;
    other.using_dedicated_ = false;
}

TransferQueue& TransferQueue::operator=(TransferQueue&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        memory_properties_ = other.memory_properties_;
        pool_ = other.pool_;
        queue_ = other.queue_;
        family_ = other.family_;
        using_dedicated_ = other.using_dedicated_;
        other.device_ = VK_NULL_HANDLE;
        other.pool_ = VK_NULL_HANDLE;
        other.queue_ = VK_NULL_HANDLE;
        other.family_ = vk::kInvalidQueueFamily;
        other.using_dedicated_ = false;
    }
    return *this;
}

core::Result<TransferQueue> TransferQueue::create(const vk::Device& device,
                                                  const vk::PhysicalDeviceInfo& gpu) {
    TransferQueue transfer;
    transfer.device_ = device.handle();
    transfer.memory_properties_ = gpu.memory_properties;

    if (device.has_dedicated_transfer_queue()) {
        transfer.family_ = device.transfer_family();
        transfer.queue_ = device.transfer_queue();
        transfer.using_dedicated_ = true;
    } else {
        transfer.family_ = device.graphics_family();
        transfer.queue_ = device.graphics_queue();
        transfer.using_dedicated_ = false;
    }

    const VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = transfer.family_,
    };

    if (const VkResult result = vkCreateCommandPool(transfer.device_, &pool_info, nullptr, &transfer.pool_);
        result != VK_SUCCESS) {
        return core::Result<TransferQueue>::failure(core::ErrorCode::VulkanFailure,
                                                    "vkCreateCommandPool (transfer) failed",
                                                    static_cast<core::i32>(result));
    }

    AEGIS_LOG_INFO("[aegis] transfer queue ready (family " << transfer.family_ << ", "
                   << (transfer.using_dedicated_ ? "dedicated" : "graphics fallback") << ")");
    return core::Result<TransferQueue>::ok(std::move(transfer));
}

core::Result<void> TransferQueue::upload(StagingRing& ring, const VkBuffer dst_buffer,
                                         const u64 dst_offset, const void* const data,
                                         const u64 size) {
    if (size == 0) {
        return core::Result<void>::ok();
    }
    const StagingSlice slice = ring.write(data, size, kDefaultAlignment);
    if (slice.data == nullptr) {
        return core::Result<void>::failure(core::ErrorCode::InvalidArgument,
                                           "staging ring write failed (upload larger than the ring?)");
    }

    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer) != VK_SUCCESS) {
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkAllocateCommandBuffers (transfer) failed");
    }

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, pool_, 1, &command_buffer);
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkBeginCommandBuffer (transfer) failed");
    }

    const VkBufferCopy region{.srcOffset = slice.offset, .dstOffset = dst_offset, .size = size};
    vkCmdCopyBuffer(command_buffer, ring.buffer(), dst_buffer, 1, &region);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, pool_, 1, &command_buffer);
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkEndCommandBuffer (transfer) failed");
    }

    core::Result<void> result = submit_and_wait(command_buffer);
    vkFreeCommandBuffers(device_, pool_, 1, &command_buffer);
    return result;
}

core::Result<void> TransferQueue::readback(const VkBuffer src_buffer, const u64 src_offset,
                                           void* const dst_data, const u64 size) {
    if (size == 0) {
        return core::Result<void>::ok();
    }

    // Host-visible scratch buffer the GPU can copy into; freed automatically.
    GpuBufferDesc scratch_desc;
    scratch_desc.size = size;
    scratch_desc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    scratch_desc.memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    auto scratch = GpuBuffer::create(device_, memory_properties_, scratch_desc);
    if (!scratch) {
        return core::Result<void>::failure(scratch.error().code, scratch.error().message,
                                           scratch.error().native_code);
    }

    const VkCommandBufferAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device_, &allocate_info, &command_buffer) != VK_SUCCESS) {
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkAllocateCommandBuffers (readback) failed");
    }

    const VkCommandBufferBeginInfo begin_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, pool_, 1, &command_buffer);
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkBeginCommandBuffer (readback) failed");
    }

    const VkBufferCopy region{.srcOffset = src_offset, .dstOffset = 0, .size = size};
    vkCmdCopyBuffer(command_buffer, src_buffer, scratch.value().handle(), 1, &region);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(device_, pool_, 1, &command_buffer);
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkEndCommandBuffer (readback) failed");
    }

    core::Result<void> result = submit_and_wait(command_buffer);
    vkFreeCommandBuffers(device_, pool_, 1, &command_buffer);
    if (!result) {
        return result;
    }

    std::memcpy(dst_data, scratch.value().mapped(), size);
    return core::Result<void>::ok();
}

core::Result<void> TransferQueue::submit_and_wait(const VkCommandBuffer command_buffer) {
    const VkFenceCreateInfo fence_info{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };

    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fence_info, nullptr, &fence) != VK_SUCCESS) {
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkCreateFence (transfer) failed");
    }

    const VkSubmitInfo submit_info{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    if (const VkResult result = vkQueueSubmit(queue_, 1, &submit_info, fence); result != VK_SUCCESS) {
        vkDestroyFence(device_, fence, nullptr);
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkQueueSubmit (transfer) failed",
                                           static_cast<core::i32>(result));
    }

    const VkResult wait_result = vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device_, fence, nullptr);
    if (wait_result != VK_SUCCESS) {
        return core::Result<void>::failure(core::ErrorCode::VulkanFailure,
                                           "vkWaitForFences (transfer) failed",
                                           static_cast<core::i32>(wait_result));
    }
    return core::Result<void>::ok();
}

void TransferQueue::reset() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        if (pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
        }
    }
    device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    family_ = vk::kInvalidQueueFamily;
    using_dedicated_ = false;
}

} // namespace aegis::memory
