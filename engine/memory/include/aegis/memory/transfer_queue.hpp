// Aegis2D — blocking transfer queue for load-time uploads and readbacks.
#pragma once

#include <aegis/export.h>

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/memory/constants.hpp>
#include <aegis/memory/staging_ring.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/physical_device.hpp>

#include <vector>

namespace aegis::memory {

/// Uploads/readbacks submitted to a transfer-only queue family when the GPU
/// exposes one (ASYNC-DMA overlap with graphics), otherwise to the graphics
/// queue. All operations are BLOCKING (fence wait) — they are intended for
/// load time, not per frame.
class AEGIS_API TransferQueue : public core::NonCopyable {
public:
    TransferQueue() = default;
    ~TransferQueue();
    TransferQueue(TransferQueue&& other) noexcept;
    TransferQueue& operator=(TransferQueue&& other) noexcept;

    [[nodiscard]] static core::Result<TransferQueue> create(const vk::Device& device,
                                                            const vk::PhysicalDeviceInfo& gpu);

    /// Stages `size` bytes of `data` through `ring` into `dst_buffer` at
    /// `dst_offset`. The staging ring is left un-reset; callers reset it once
    /// all uploads for a batch are submitted.
    [[nodiscard]] core::Result<void> upload(StagingRing& ring,
                                            VkBuffer dst_buffer,
                                            core::u64 dst_offset,
                                            const void* data,
                                            core::u64 size);

    /// Device-to-host readback into `dst_data` (must be `size` bytes). Uses a
    /// temporary host-visible scratch buffer internally.
    [[nodiscard]] core::Result<void> readback(VkBuffer src_buffer,
                                              core::u64 src_offset,
                                              void* dst_data,
                                              core::u64 size);

    bool using_dedicated_queue() const noexcept { return using_dedicated_; }

private:
    void reset() noexcept;
    core::Result<void> submit_and_wait(VkCommandBuffer command_buffer);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory_properties_{};
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    core::u32 family_ = vk::kInvalidQueueFamily;
    bool using_dedicated_ = false;
};

} // namespace aegis::memory
