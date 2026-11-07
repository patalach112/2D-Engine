#include <aegis/memory/buffer_arena.hpp>

#include <aegis/core/assert.hpp>
#include <aegis/core/log.hpp>

#include <utility>
#include <vector>

namespace aegis::memory {

using core::u64;

BufferArena::~BufferArena() {
    AEGIS_ASSERT_MSG(outstanding_bytes_ == 0,
                     "BufferArena destroyed with outstanding allocations (BufferAllocation must not outlive the arena)");
    reset();
}

BufferArena::BufferArena(BufferArena&& other) noexcept
    : block_(std::move(other.block_)),
      free_list_(std::move(other.free_list_)),
      outstanding_bytes_(other.outstanding_bytes_),
      live_allocations_(other.live_allocations_) {
    other.outstanding_bytes_ = 0;
    other.live_allocations_ = 0;
}

BufferArena& BufferArena::operator=(BufferArena&& other) noexcept {
    if (this != &other) {
        AEGIS_ASSERT_MSG(outstanding_bytes_ == 0, "moving into a BufferArena with outstanding allocations");
        reset();
        block_ = std::move(other.block_);
        free_list_ = std::move(other.free_list_);
        outstanding_bytes_ = other.outstanding_bytes_;
        live_allocations_ = other.live_allocations_;
        other.outstanding_bytes_ = 0;
        other.live_allocations_ = 0;
    }
    return *this;
}

core::Result<BufferArena> BufferArena::create(const vk::Device& device,
                                              const vk::PhysicalDeviceInfo& gpu,
                                              const BufferArenaDesc& desc) {
    if (desc.capacity == 0) {
        return core::Result<BufferArena>::failure(core::ErrorCode::InvalidArgument,
                                                  "BufferArena capacity must be non-zero");
    }

    // CONCURRENT across graphics + dedicated transfer families when both exist.
    std::vector<core::u32> families{device.graphics_family()};
    if (device.has_dedicated_transfer_queue() && device.transfer_family() != device.graphics_family()) {
        families.push_back(device.transfer_family());
    }

    GpuBufferDesc buffer_desc;
    buffer_desc.size = desc.capacity;
    buffer_desc.usage = desc.usage;
    buffer_desc.memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (families.size() >= 2) {
        buffer_desc.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        buffer_desc.concurrent_families = families;
    }

    auto buffer = GpuBuffer::create(device.handle(), gpu.memory_properties, buffer_desc);
    if (!buffer) {
        return core::Result<BufferArena>::failure(buffer.error().code, buffer.error().message,
                                                  buffer.error().native_code);
    }

    BufferArena arena;
    arena.block_ = std::move(buffer.value());
    arena.free_list_ = FreeListAllocator(desc.capacity);
    arena.outstanding_bytes_ = 0;
    arena.live_allocations_ = 0;

    AEGIS_LOG_INFO("[aegis] device-local arena created: " << (desc.capacity >> 20) << " MiB ("
                   << (families.size() >= 2 ? "CONCURRENT graphics+transfer" : "EXCLUSIVE graphics")
                   << ")");
    return core::Result<BufferArena>::ok(std::move(arena));
}

core::Result<BufferAllocation> BufferArena::allocate(const u64 size, const u64 alignment) {
    auto allocation = free_list_.allocate(size, alignment);
    if (!allocation) {
        return core::Result<BufferAllocation>::failure(allocation.error().code,
                                                       allocation.error().message);
    }

    outstanding_bytes_ += size;
    ++live_allocations_;
    return core::Result<BufferAllocation>::ok(BufferAllocation(this, allocation.value()));
}

void BufferArena::deallocate(const Allocation& allocation) noexcept {
    free_list_.deallocate(allocation);
    outstanding_bytes_ -= allocation.size;
    --live_allocations_;
}

void BufferArena::reset() noexcept {
    block_ = GpuBuffer{};
}

// ---------------------------------------------------------------------------
// BufferAllocation
// ---------------------------------------------------------------------------

BufferAllocation::~BufferAllocation() {
    if (arena_ != nullptr) {
        arena_->deallocate(allocation_);
        arena_ = nullptr;
    }
}

BufferAllocation::BufferAllocation(BufferAllocation&& other) noexcept
    : arena_(other.arena_), allocation_(other.allocation_) {
    other.arena_ = nullptr;
}

BufferAllocation& BufferAllocation::operator=(BufferAllocation&& other) noexcept {
    if (this != &other) {
        if (arena_ != nullptr) {
            arena_->deallocate(allocation_);
        }
        arena_ = other.arena_;
        allocation_ = other.allocation_;
        other.arena_ = nullptr;
    }
    return *this;
}

VkBuffer BufferAllocation::buffer() const noexcept {
    return arena_ != nullptr ? arena_->block_.handle() : VK_NULL_HANDLE;
}

} // namespace aegis::memory
