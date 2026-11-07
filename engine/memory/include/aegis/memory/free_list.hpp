// Aegis2D — pure best-fit free-list allocator.
//
// No Vulkan types anywhere in this file: the allocator is a byte-range
// manager and is unit-tested without a driver.
#pragma once

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>

#include <vector>

namespace aegis::memory {

struct Allocation {
    core::u64 offset = 0;
    core::u64 size = 0;
};

/// Best-fit allocator over a single [0, capacity) byte range.
///
/// - allocate(): finds the smallest free block that can satisfy `size` after
///   applying `alignment`; splits the remainder. Returns a failure Result when
///   the range is exhausted.
/// - deallocate(): inserts the range back and coalesces with neighbours.
/// - largest_free_block(): size of the biggest contiguous free range.
///
/// Not thread-safe; callers serialize access (the engine owns allocation at
/// load time).
class FreeListAllocator {
public:
    explicit FreeListAllocator(core::u64 capacity);

    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;
    FreeListAllocator(FreeListAllocator&&) noexcept = default;
    FreeListAllocator& operator=(FreeListAllocator&&) noexcept = default;

    [[nodiscard]] core::Result<Allocation> allocate(core::u64 size, core::u64 alignment);
    void deallocate(const Allocation& allocation) noexcept;

    [[nodiscard]] core::u64 capacity() const noexcept { return capacity_; }
    [[nodiscard]] core::u64 allocated_bytes() const noexcept { return allocated_bytes_; }
    [[nodiscard]] core::u64 available_bytes() const noexcept { return capacity_ - allocated_bytes_; }
    [[nodiscard]] core::u64 live_allocations() const noexcept { return live_allocations_; }
    [[nodiscard]] core::u64 largest_free_block() const noexcept;

private:
    struct FreeBlock {
        core::u64 offset = 0;
        core::u64 size = 0;
    };

    core::u64 capacity_ = 0;
    core::u64 allocated_bytes_ = 0;
    core::u64 live_allocations_ = 0;
    std::vector<FreeBlock> free_;  // sorted by offset, non-overlapping, never coalesced-yet
};

} // namespace aegis::memory
