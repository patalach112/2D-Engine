// Aegis2D — Milestone 2 verification tool.
//
// Headless end-to-end check of the memory path:
//   pattern → StagingRing → TransferQueue (dedicated family when present)
//   → device-local BufferArena → TransferQueue readback → byte-compare.
//
// Exit codes:
//   0 — verification passed, or skipped because no Vulkan loader/driver exists
//       in this environment (container/CI). A skip is not a failure.
//   1 — verification ran and found a real mismatch / engine error.
#include <aegis/core/log.hpp>
#include <aegis/core/types.hpp>
#include <aegis/memory/buffer_arena.hpp>
#include <aegis/memory/constants.hpp>
#include <aegis/memory/staging_ring.hpp>
#include <aegis/memory/transfer_queue.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/instance.hpp>
#include <aegis/vk/physical_device.hpp>

#include <cstring>
#include <vector>

namespace {

using aegis::core::u64;
using aegis::core::u8;

/// xorshift64* — deterministic pseudo-random bytes for the test pattern.
struct XorShift64 {
    u64 state;
    explicit XorShift64(u64 seed) : state(seed | 1) {}
    u64 next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 2685821657736338717ULL;
    }
};

std::vector<u8> make_pattern(const u64 size, const u64 seed) {
    std::vector<u8> bytes(size);
    XorShift64 rng(seed);
    for (u64 offset = 0; offset < size; offset += 8) {
        const u64 value = rng.next();
        const u64 chunk = std::min<u64>(8, size - offset);
        std::memcpy(bytes.data() + offset, &value, chunk);
    }
    return bytes;
}

bool verify_segment(const char* name, const std::vector<u8>& expected, const std::vector<u8>& actual) {
    if (expected.size() != actual.size()) {
        AEGIS_LOG_ERROR("[roundtrip] " << name << ": size mismatch (expected " << expected.size()
                        << " B, got " << actual.size() << " B)");
        return false;
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != actual[i]) {
            AEGIS_LOG_ERROR("[roundtrip] " << name << ": first mismatch at byte " << i
                            << " (expected 0x" << std::hex << static_cast<unsigned>(expected[i])
                            << ", got 0x" << static_cast<unsigned>(actual[i]) << ")");
            return false;
        }
    }
    AEGIS_LOG_INFO("[roundtrip] " << name << ": " << expected.size() << " bytes verified");
    return true;
}

} // namespace

int main() {
    AEGIS_LOG_INFO("[roundtrip] Milestone 2: arena → staging → transfer → readback → byte-compare");

    aegis::vk::InstanceConfig instance_config;
    instance_config.application_name = "Aegis2D Roundtrip";
    instance_config.enable_validation = false;  // headless tool; keep output clean

    auto instance_result = aegis::vk::Instance::create(instance_config);
    if (!instance_result.has_value()) {
        AEGIS_LOG_INFO("[roundtrip] SKIP: no Vulkan loader/driver in this environment ("
                       << instance_result.error().message << ")");
        return 0;
    }
    aegis::vk::Instance instance = std::move(instance_result).take();

    auto gpu_result = aegis::vk::select_physical_device(instance.handle(), VK_NULL_HANDLE);
    if (!gpu_result.has_value()) {
        AEGIS_LOG_INFO("[roundtrip] SKIP: " << gpu_result.error().message);
        return 0;
    }
    const aegis::vk::PhysicalDeviceInfo gpu = std::move(gpu_result).take();

    aegis::vk::DeviceConfig device_config;
    device_config.surface = VK_NULL_HANDLE;
    device_config.enable_swapchain = false;  // headless: no presentation
    auto device_result = aegis::vk::Device::create(gpu, device_config);
    if (!device_result.has_value()) {
        AEGIS_LOG_ERROR("[roundtrip] device creation failed: " << device_result.error().to_string());
        return 1;
    }
    aegis::vk::Device device = std::move(device_result).take();

    constexpr u64 kArenaCapacity = 4ull << 20;  // 4 MiB
    constexpr u64 kVertexBytes = 64ull << 10;   // 64 KiB
    constexpr u64 kIndexBytes = 16ull << 10;    // 16 KiB

    aegis::memory::BufferArenaDesc arena_desc;
    arena_desc.capacity = kArenaCapacity;
    auto arena_result = aegis::memory::BufferArena::create(device, gpu, arena_desc);
    if (!arena_result.has_value()) {
        AEGIS_LOG_ERROR("[roundtrip] arena creation failed: " << arena_result.error().to_string());
        return 1;
    }
    aegis::memory::BufferArena arena = std::move(arena_result).take();

    {
        auto vertex = arena.allocate(kVertexBytes, 256);
        auto index = arena.allocate(kIndexBytes, 16);
        if (!vertex.has_value() || !index.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] arena allocation failed");
            return 1;
        }
        AEGIS_LOG_INFO("[roundtrip] arena allocations: vertex at offset " << vertex.value().offset()
                       << " (alignment 256), index at offset " << index.value().offset() << " (alignment 16)");

        const std::vector<u8> vertex_pattern = make_pattern(kVertexBytes, 0x123456789abcdef0ULL);
        const std::vector<u8> index_pattern = make_pattern(kIndexBytes, 0x0fedcba987654321ULL);

        auto ring_result = aegis::memory::StagingRing::create(device, gpu, 1ull << 20);  // 1 MiB
        if (!ring_result.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] staging ring creation failed: " << ring_result.error().to_string());
            return 1;
        }
        aegis::memory::StagingRing ring = std::move(ring_result).take();

        auto transfer_result = aegis::memory::TransferQueue::create(device, gpu);
        if (!transfer_result.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] transfer queue creation failed: " << transfer_result.error().to_string());
            return 1;
        }
        aegis::memory::TransferQueue transfer = std::move(transfer_result).take();
        AEGIS_LOG_INFO("[roundtrip] using " << (transfer.using_dedicated_queue()
                                                    ? "the dedicated transfer queue"
                                                    : "the graphics queue (no dedicated TRANSFER family)"));

        if (auto r = transfer.upload(ring, arena.buffer(), vertex.value().offset(),
                                     vertex_pattern.data(), kVertexBytes);
            !r.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] vertex upload failed: " << r.error().to_string());
            return 1;
        }
        if (auto r = transfer.upload(ring, arena.buffer(), index.value().offset(), index_pattern.data(),
                                     kIndexBytes);
            !r.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] index upload failed: " << r.error().to_string());
            return 1;
        }
        ring.reset();

        std::vector<u8> vertex_readback(kVertexBytes);
        if (auto r = transfer.readback(arena.buffer(), vertex.value().offset(), vertex_readback.data(),
                                       kVertexBytes);
            !r.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] vertex readback failed: " << r.error().to_string());
            return 1;
        }
        std::vector<u8> index_readback(kIndexBytes);
        if (auto r = transfer.readback(arena.buffer(), index.value().offset(), index_readback.data(),
                                       kIndexBytes);
            !r.has_value()) {
            AEGIS_LOG_ERROR("[roundtrip] index readback failed: " << r.error().to_string());
            return 1;
        }

        if (!verify_segment("vertex buffer", vertex_pattern, vertex_readback)) {
            return 1;
        }
        if (!verify_segment("index buffer", index_pattern, index_readback)) {
            return 1;
        }

        AEGIS_LOG_INFO("[roundtrip] arena: " << arena.outstanding_bytes() << " bytes outstanding across "
                       << arena.live_allocations() << " allocation(s); largest free block: "
                       << arena.largest_free_block() << " B");
    }

    // Allocations have been destroyed here — the arena destructor asserts the
    // arena returned to a fully-free state (it does, via coalescing).
    AEGIS_LOG_INFO("[roundtrip] ROUNDTRIP VERIFIED");
    return 0;
}
