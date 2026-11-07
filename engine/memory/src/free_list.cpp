#include <aegis/memory/free_list.hpp>

#include <aegis/core/assert.hpp>

#include <algorithm>
#include <limits>

namespace aegis::memory {

using core::u64;

namespace {

constexpr u64 align_up(const u64 value, const u64 alignment) noexcept {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

} // namespace

FreeListAllocator::FreeListAllocator(const u64 capacity) : capacity_(capacity) {
    AEGIS_ASSERT_MSG(capacity > 0, "FreeListAllocator capacity must be non-zero");
    if (capacity > 0) {
        free_.push_back(FreeBlock{0, capacity});
    }
}

core::Result<Allocation> FreeListAllocator::allocate(const u64 size, const u64 alignment) {
    if (size == 0) {
        return core::Result<Allocation>::failure(core::ErrorCode::InvalidArgument,
                                                 "allocate(size=0) is not allowed");
    }
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return core::Result<Allocation>::failure(core::ErrorCode::InvalidArgument,
                                                 "alignment must be a non-zero power of two");
    }

    // ---- Best fit: smallest block that fits `size` after aligning the start.
    std::size_t best_index = free_.size();
    u64 best_block_size = std::numeric_limits<u64>::max();
    u64 best_offset = 0;

    for (std::size_t i = 0; i < free_.size(); ++i) {
        const u64 offset = free_[i].offset;
        const u64 aligned = align_up(offset, alignment);
        const u64 head = aligned - offset;
        if (free_[i].size < head || free_[i].size - head < size) {
            continue;
        }
        const u64 block_size = free_[i].size;
        if (block_size < best_block_size ||
            (block_size == best_block_size && aligned < best_offset)) {
            best_index = i;
            best_block_size = block_size;
            best_offset = aligned;
        }
    }

    if (best_index == free_.size()) {
        return core::Result<Allocation>::failure(core::ErrorCode::OutOfMemory,
                                                 "free list exhausted (requested " + std::to_string(size) +
                                                     " B, alignment " + std::to_string(alignment) + ")");
    }

    const FreeBlock block = free_[best_index];
    const u64 head = best_offset - block.offset;
    const u64 tail = (block.offset + block.size) - (best_offset + size);

    // Rebuild the block list entry: up to two fragments remain.
    free_.erase(free_.begin() + static_cast<std::ptrdiff_t>(best_index));
    if (head != 0) {
        free_.push_back(FreeBlock{block.offset, head});
    }
    if (tail != 0) {
        free_.push_back(FreeBlock{best_offset + size, tail});
    }
    // Keep the list sorted by offset (insertion sort, list is nearly sorted).
    std::sort(free_.begin(), free_.end(),
              [](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });

    allocated_bytes_ += size;
    ++live_allocations_;
    return core::Result<Allocation>::ok(Allocation{best_offset, size});
}

void FreeListAllocator::deallocate(const Allocation& allocation) noexcept {
    AEGIS_ASSERT_MSG(allocation.size > 0, "deallocate(size=0) is not allowed");
    AEGIS_ASSERT_MSG(allocation.offset + allocation.size <= capacity_,
                     "deallocate(range outside arena)");

    free_.push_back(FreeBlock{allocation.offset, allocation.size});
    std::sort(free_.begin(), free_.end(),
              [](const FreeBlock& a, const FreeBlock& b) { return a.offset < b.offset; });

    // ---- Coalesce adjacent blocks.
    std::vector<FreeBlock> merged;
    merged.reserve(free_.size());
    for (const FreeBlock& block : free_) {
        if (!merged.empty() && merged.back().offset + merged.back().size == block.offset) {
            merged.back().size += block.size;
        } else {
            merged.push_back(block);
        }
    }
    free_ = std::move(merged);

    allocated_bytes_ -= allocation.size;
    --live_allocations_;
}

u64 FreeListAllocator::largest_free_block() const noexcept {
    u64 largest = 0;
    for (const FreeBlock& block : free_) {
        largest = std::max(largest, block.size);
    }
    return largest;
}

} // namespace aegis::memory
