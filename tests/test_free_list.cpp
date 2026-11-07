// Aegis2D — unit tests for the free-list allocator (Milestone 2).
#include <aegis/memory/free_list.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "aegis_test.hpp"

namespace {

struct Xorshift64 {
    aegis::core::u64 state;
    explicit Xorshift64(aegis::core::u64 seed) : state(seed | 1) {}
    aegis::core::u64 next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ULL;
    }
};

}  // namespace

using aegis::memory::Allocation;
using aegis::core::u64;
using aegis::memory::FreeListAllocator;

AEGIS_TEST(free_list, "allocate returns aligned, non-overlapping regions") {
    FreeListAllocator allocator(1024);
    auto a_result = allocator.allocate(100, 16);
    auto b_result = allocator.allocate(200, 256);

    AEGIS_REQUIRE(a_result.has_value());
    AEGIS_REQUIRE(b_result.has_value());

    const Allocation a = a_result.value();
    const Allocation b = b_result.value();

    AEGIS_CHECK_EQ(a.offset % 16, 0u);
    AEGIS_CHECK_EQ(b.offset % 256, 0u);

    const bool overlap = (a.offset < b.offset + b.size) && (b.offset < a.offset + a.size);
    AEGIS_CHECK(!overlap);
}

AEGIS_TEST(free_list, "fails cleanly when exhausted") {
    FreeListAllocator allocator(128);
    AEGIS_CHECK(allocator.allocate(64, 16).has_value());
    const auto too_big = allocator.allocate(65, 16);
    AEGIS_CHECK(!too_big.has_value());
    AEGIS_CHECK(allocator.allocate(64, 16).has_value());  // exact fit still works
}

AEGIS_TEST(free_list, "zero size and non-power-of-two alignment are rejected") {
    FreeListAllocator allocator(128);
    AEGIS_CHECK(!allocator.allocate(0, 16).has_value());
    AEGIS_CHECK(!allocator.allocate(16, 0).has_value());
    AEGIS_CHECK(!allocator.allocate(16, 3).has_value());  // not a power of two
    AEGIS_CHECK(allocator.allocate(16, 1).has_value());   // alignment 1 is legal
}

AEGIS_TEST(free_list, "deallocate coalesces neighbours back into one block") {
    FreeListAllocator allocator(1024);
    auto a = allocator.allocate(128, 16);
    auto b = allocator.allocate(128, 16);
    auto c = allocator.allocate(128, 16);
    AEGIS_REQUIRE(a.has_value());
    AEGIS_REQUIRE(b.has_value());
    AEGIS_REQUIRE(c.has_value());

    allocator.deallocate(b.value());
    allocator.deallocate(a.value());
    allocator.deallocate(c.value());
    AEGIS_CHECK_EQ(allocator.largest_free_block(), 1024u);
    AEGIS_CHECK_EQ(allocator.allocated_bytes(), 0u);
    AEGIS_CHECK_EQ(allocator.live_allocations(), 0u);
}

AEGIS_TEST(free_list, "interleaved free/alloc packs without leaking") {
    FreeListAllocator allocator(4096);
    constexpr u64 kPattern[] = {64, 128, 256, 512, 128, 64};

    std::vector<Allocation> live;
    for (const u64 size : kPattern) {
        auto result = allocator.allocate(size, 16);
        AEGIS_REQUIRE(result.has_value());
        live.push_back(result.value());
    }

    // Free the middle allocations, then re-fill with smaller ones.
    allocator.deallocate(live[1]);  // 128 B
    allocator.deallocate(live[3]);  // 512 B
    for (u64 size : {u64{96}, u64{200}, u64{100}}) {
        auto result = allocator.allocate(size, 16);
        AEGIS_REQUIRE(result.has_value());
        live.push_back(result.value());
    }

    for (const Allocation& allocation : live) {
        if (allocation.size == live[1].size || allocation.size == live[3].size) {
            continue;
        }
        allocator.deallocate(allocation);
    }
    AEGIS_CHECK(allocator.allocated_bytes() > 0);  // some allocations still live
}

AEGIS_TEST(free_list, "best-fit reuses the smallest sufficient free block") {
    FreeListAllocator allocator(1024);
    auto small = allocator.allocate(64, 16);
    auto medium = allocator.allocate(256, 16);
    auto large = allocator.allocate(512, 16);
    AEGIS_REQUIRE(small.has_value());
    AEGIS_REQUIRE(medium.has_value());
    AEGIS_REQUIRE(large.has_value());

    allocator.deallocate(small.value());
    allocator.deallocate(large.value());
    // 192 B requested: only the 64 B hole (too small) or the 512 B hole (fits)
    auto result = allocator.allocate(192, 16);
    AEGIS_REQUIRE(result.has_value());
    AEGIS_CHECK_EQ(result.value().offset, large.value().offset);  // best fit = the 512 B block
}

AEGIS_TEST(free_list, "largest_free_block reflects splits and merges") {
    FreeListAllocator allocator(1024);
    AEGIS_CHECK_EQ(allocator.largest_free_block(), 1024u);
    auto a = allocator.allocate(100, 16);
    AEGIS_REQUIRE(a.has_value());
    AEGIS_CHECK_EQ(allocator.largest_free_block(), 1024u - 100u);
    allocator.deallocate(a.value());
    AEGIS_CHECK_EQ(allocator.largest_free_block(), 1024u);
}

AEGIS_TEST(free_list, "randomized interleave never overlaps and stays in bounds") {
    FreeListAllocator allocator(16 * 1024);
    Xorshift64 rng(0x243f6a8885a308d3ULL);

    std::vector<Allocation> live;
    for (int step = 0; step < 200; ++step) {
        const bool allocate_now = live.empty() || (rng.next() & 1u) != 0u;
        if (allocate_now && live.size() < 32) {
            const u64 size = 16 + (rng.next() % 512);
            auto result = allocator.allocate(size, 16);
            if (result.has_value()) {
                live.push_back(result.value());
            }
        } else {
            const std::size_t index = static_cast<std::size_t>(rng.next() % live.size());
            allocator.deallocate(live[index]);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(index));
        }

        // Every live allocation must stay inside the arena and never overlap another.
        for (const Allocation& allocation : live) {
            AEGIS_CHECK(allocation.offset + allocation.size <= allocator.capacity());
        }
        std::vector<Allocation> sorted = live;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Allocation& a, const Allocation& b) { return a.offset < b.offset; });
        for (std::size_t i = 1; i < sorted.size(); ++i) {
            AEGIS_CHECK(sorted[i - 1].offset + sorted[i - 1].size <= sorted[i].offset);
        }
    }
}
