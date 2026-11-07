#include <aegis/memory/staging_ring.hpp>

#include <aegis/core/assert.hpp>
#include <aegis/core/log.hpp>

#include <cstring>
#include <string>
#include <vector>

namespace aegis::memory {

using core::u64;

namespace {

constexpr u64 align_up(const u64 value, const u64 alignment) noexcept {
    return (value + (alignment - 1)) & ~(alignment - 1);
}

} // namespace

StagingRing::~StagingRing() = default;

StagingRing::StagingRing(StagingRing&& other) noexcept
    : buffer_(std::move(other.buffer_)),
      capacity_(other.capacity_),
      write_offset_(other.write_offset_),
      pending_bytes_(other.pending_bytes_),
      wrap_count_(other.wrap_count_) {
    other.capacity_ = 0;
    other.write_offset_ = 0;
    other.pending_bytes_ = 0;
    other.wrap_count_ = 0;
}

StagingRing& StagingRing::operator=(StagingRing&& other) noexcept {
    if (this != &other) {
        buffer_ = std::move(other.buffer_);
        capacity_ = other.capacity_;
        write_offset_ = other.write_offset_;
        pending_bytes_ = other.pending_bytes_;
        wrap_count_ = other.wrap_count_;
        other.capacity_ = 0;
        other.write_offset_ = 0;
        other.pending_bytes_ = 0;
        other.wrap_count_ = 0;
    }
    return *this;
}

core::Result<StagingRing> StagingRing::create(const vk::Device& device,
                                              const vk::PhysicalDeviceInfo& gpu,
                                              const u64 capacity) {
    if (capacity == 0) {
        return core::Result<StagingRing>::failure(core::ErrorCode::InvalidArgument,
                                                  "StagingRing capacity must be non-zero");
    }

    // CONCURRENT across graphics + dedicated transfer families when both exist.
    std::vector<core::u32> families{device.graphics_family()};
    if (device.has_dedicated_transfer_queue() && device.transfer_family() != device.graphics_family()) {
        families.push_back(device.transfer_family());
    }

    GpuBufferDesc desc;
    desc.size = capacity;
    desc.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    desc.memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (families.size() >= 2) {
        desc.sharing_mode = VK_SHARING_MODE_CONCURRENT;
        desc.concurrent_families = families;
    }

    auto buffer = GpuBuffer::create(device.handle(), gpu.memory_properties, desc);
    if (!buffer) {
        return core::Result<StagingRing>::failure(buffer.error().code, buffer.error().message,
                                                  buffer.error().native_code);
    }

    StagingRing ring;
    ring.buffer_ = std::move(buffer.value());
    ring.capacity_ = capacity;

    AEGIS_LOG_INFO("[aegis] staging ring created: " << (capacity >> 20) << " MiB ("
                   << (families.size() >= 2 ? "CONCURRENT graphics+transfer" : "EXCLUSIVE graphics")
                   << ")");
    return core::Result<StagingRing>::ok(std::move(ring));
}

StagingSlice StagingRing::write(const void* const data, const u64 size, const u64 alignment) {
    AEGIS_ASSERT_MSG(data != nullptr, "StagingRing::write data must not be null");
    AEGIS_ASSERT_MSG(alignment != 0 && (alignment & (alignment - 1)) == 0,
                     "StagingRing::write alignment must be a non-zero power of two");

    if (size > capacity_) {
        AEGIS_LOG_ERROR("[aegis] staging ring too small: requested " << size << " B, capacity "
                        << capacity_ << " B");
        return StagingSlice{};
    }

    u64 offset = align_up(write_offset_, alignment);
    if (offset + size > capacity_) {
        if (write_offset_ != 0) {
            AEGIS_LOG_DEBUG("[aegis] staging ring wrap (wasted tail: " << (capacity_ - write_offset_)
                            << " B)");
        }
        ++wrap_count_;
        offset = 0;
    }

    std::memcpy(static_cast<char*>(buffer_.mapped()) + offset, data, size);
    buffer_.flush();  // no-op for coherent memory (the common case)

    StagingSlice slice{offset, size, static_cast<const char*>(buffer_.mapped()) + offset};
    write_offset_ = offset + size;
    pending_bytes_ += size;
    return slice;
}

void StagingRing::reset() noexcept {
    write_offset_ = 0;
    pending_bytes_ = 0;
}

void StagingRing::reset_internal() noexcept { reset(); }

} // namespace aegis::memory
