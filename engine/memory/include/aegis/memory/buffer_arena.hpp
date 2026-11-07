// Aegis2D — device-local arena: one large VkBuffer, best-fit sub-allocation.
#pragma once

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/memory/free_list.hpp>
#include <aegis/memory/gpu_buffer.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/physical_device.hpp>

namespace aegis::memory {

struct BufferArenaDesc {
    /// Total bytes of the device-local block.
    core::u64 capacity = 0;
    /// Default: VERTEX|INDEX|STORAGE|TRANSFER_SRC|TRANSFER_DST.
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT;
};

class BufferAllocation;

class BufferArena : public core::NonCopyable {
public:
    BufferArena() = default;
    ~BufferArena();
    BufferArena(BufferArena&&) noexcept;
    BufferArena& operator=(BufferArena&&) noexcept;

    [[nodiscard]] static core::Result<BufferArena> create(const vk::Device& device,
                                                          const vk::PhysicalDeviceInfo& gpu,
                                                          const BufferArenaDesc& desc);

    /// Sub-allocate `size` bytes with the given power-of-two alignment.
    [[nodiscard]] core::Result<BufferAllocation> allocate(core::u64 size, core::u64 alignment);

    /// Device-local VkBuffer backing the arena (shared by all allocations).
    VkBuffer buffer() const noexcept { return block_.handle(); }
    core::u64 capacity() const noexcept { return block_.size(); }
    core::u64 outstanding_bytes() const noexcept { return outstanding_bytes_; }
    core::u64 live_allocations() const noexcept { return live_allocations_; }
    core::u64 largest_free_block() const noexcept { return free_list_.largest_free_block(); }

private:
    void deallocate(const Allocation& allocation) noexcept;
    void reset() noexcept;

    GpuBuffer block_;
    FreeListAllocator free_list_{1};  // placeholder capacity, replaced in create()
    core::u64 outstanding_bytes_ = 0;
    core::u64 live_allocations_ = 0;

    friend class BufferAllocation;
};

/// Move-only RAII handle for one arena sub-allocation. Returns the segment to
/// the arena's free list on destruction (coalescing neighbours).
class BufferAllocation : public core::NonCopyable {
public:
    BufferAllocation() = default;
    ~BufferAllocation();
    BufferAllocation(BufferAllocation&& other) noexcept;
    BufferAllocation& operator=(BufferAllocation&& other) noexcept;

    core::u64 offset() const noexcept { return allocation_.offset; }
    core::u64 size() const noexcept { return allocation_.size; }
    VkBuffer buffer() const noexcept;
    bool valid() const noexcept { return arena_ != nullptr; }

private:
    BufferAllocation(BufferArena* arena, const Allocation& allocation)
        : arena_(arena), allocation_(allocation) {}

    BufferArena* arena_ = nullptr;  // non-owning; the arena must outlive its allocations
    Allocation allocation_{};

    friend class BufferArena;
};

} // namespace aegis::memory
