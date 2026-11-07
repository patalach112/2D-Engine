// Aegis2D — RAII wrapper around a single VkBuffer + VkDeviceMemory.
//
// Intended for long-lived, individually-sized GPU buffers (staging blocks,
// readback scratch, device-local arena blocks). Use BufferArena for small
// sub-allocated buffers instead of creating many tiny GpuBuffers.
#pragma once

#include <aegis/export.h>

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>
#include <aegis/vk/device.hpp>

#include <vector>

namespace aegis::memory {

struct AEGIS_API GpuBufferDesc {
    core::u64 size = 0;
    VkBufferUsageFlags usage = 0;
    VkMemoryPropertyFlags memory_properties = 0;
    /// CONCURRENT buffers skip queue-family ownership transfers between the
    /// listed families (useful for staging arenas shared by graphics and the
    /// transfer queue). Requires at least two distinct families.
    VkSharingMode sharing_mode = VK_SHARING_MODE_EXCLUSIVE;
    std::vector<core::u32> concurrent_families;
};

/// Host-visible buffers are persistently mapped at creation (map/unmap per
/// upload is pointless for a load-time engine). device-local buffers return
/// nullptr from mapped().
class AEGIS_API GpuBuffer : public core::NonCopyable {
public:
    GpuBuffer() = default;
    ~GpuBuffer();

    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;

    [[nodiscard]] static core::Result<GpuBuffer> create(VkDevice device,
                                                        const VkPhysicalDeviceMemoryProperties& properties,
                                                        const GpuBufferDesc& desc);

    VkBuffer handle() const noexcept { return buffer_; }
    core::u64 size() const noexcept { return size_; }
    void* mapped() const noexcept { return mapped_; }
    bool host_coherent() const noexcept { return coherent_; }

    /// memcpy `bytes` from `data` into the mapped range at `offset`.
    /// Only valid for host-visible, mapped buffers. Flushes automatically
    /// when the memory is not device/host coherent.
    void write(const void* data, core::u64 offset, core::u64 bytes) const;
    /// Flush the persistently mapped range of a non-coherent host-visible
    /// buffer to the device. Flushes the whole buffer (offset/size are not
    /// honoured) to stay correct regardless of nonCoherentAtomSize.
    /// No-op for coherent or non-host-visible buffers.
    void flush() const;

private:
    void reset() noexcept;

    VkDevice device_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    core::u64 size_ = 0;
    void* mapped_ = nullptr;
    bool coherent_ = false;
    bool host_visible_ = false;
};

} // namespace aegis::memory
