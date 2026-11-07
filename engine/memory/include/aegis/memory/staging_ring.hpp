// Aegis2D — persistently mapped ring buffer for staging uploads.
#pragma once

#include <aegis/export.h>

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/memory/gpu_buffer.hpp>
#include <aegis/vk/device.hpp>
#include <aegis/vk/physical_device.hpp>

namespace aegis::memory {

/// View into the ring describing one staged byte range. `data` points directly
/// into the persistently mapped buffer and stays valid until the next
/// reset() call (the bytes are readable by the host immediately; the GPU may
/// read them once submitted).
struct AEGIS_API StagingSlice {
    core::u64 offset = 0;  ///< offset of the slice within the ring
    core::u64 size = 0;    ///< bytes available at the slice start
    const void* data = nullptr;
};

/// One host-visible (coherent when available) buffer, bump-allocated with
/// wrap-around. reset() marks the whole ring reusable; callers must only call
/// it once the GPU has consumed everything uploaded so far (the transfer API is
/// blocking, which makes the ordering trivial right now).
class AEGIS_API StagingRing : public core::NonCopyable {
public:
    StagingRing() = default;
    ~StagingRing();
    StagingRing(StagingRing&& other) noexcept;
    StagingRing& operator=(StagingRing&& other) noexcept;

    [[nodiscard]] static core::Result<StagingRing> create(const vk::Device& device,
                                                          const vk::PhysicalDeviceInfo& gpu,
                                                          core::u64 capacity);

    /// memcpy `size` bytes of `data` into the ring (aligned to `alignment`,
    /// which must be a non-zero power of two). Returns {0, 0, nullptr} and logs
    /// an error when `size` exceeds the ring capacity.
    [[nodiscard]] StagingSlice write(const void* data, core::u64 size, core::u64 alignment);

    /// Marks the ring as fully reusable. Callers must ensure the GPU consumed
    /// every staged upload before resetting.
    void reset() noexcept;

    VkBuffer buffer() const noexcept { return buffer_.handle(); }
    core::u64 capacity() const noexcept { return capacity_; }
    core::u64 pending_bytes() const noexcept { return pending_bytes_; }
    core::u64 wrap_count() const noexcept { return wrap_count_; }

private:
    void reset_internal() noexcept;

    GpuBuffer buffer_{};
    core::u64 capacity_ = 0;
    core::u64 write_offset_ = 0;
    core::u64 pending_bytes_ = 0;
    core::u64 wrap_count_ = 0;
};

} // namespace aegis::memory
