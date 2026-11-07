#include <aegis/memory/gpu_buffer.hpp>

#include <aegis/core/assert.hpp>
#include <aegis/core/log.hpp>
#include <aegis/memory/memory_props.hpp>

#include <cstring>
#include <utility>
#include <vector>

namespace aegis::memory {

using core::u32;
using core::u64;

GpuBuffer::~GpuBuffer() { reset(); }

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : device_(other.device_),
      buffer_(other.buffer_),
      memory_(other.memory_),
      size_(other.size_),
      mapped_(other.mapped_),
      coherent_(other.coherent_),
      host_visible_(other.host_visible_) {
    other.device_ = VK_NULL_HANDLE;
    other.buffer_ = VK_NULL_HANDLE;
    other.memory_ = VK_NULL_HANDLE;
    other.size_ = 0;
    other.mapped_ = nullptr;
    other.coherent_ = false;
    other.host_visible_ = false;
}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        reset();
        device_ = other.device_;
        buffer_ = other.buffer_;
        memory_ = other.memory_;
        size_ = other.size_;
        mapped_ = other.mapped_;
        coherent_ = other.coherent_;
        host_visible_ = other.host_visible_;
        other.device_ = VK_NULL_HANDLE;
        other.buffer_ = VK_NULL_HANDLE;
        other.memory_ = VK_NULL_HANDLE;
        other.size_ = 0;
        other.mapped_ = nullptr;
        other.coherent_ = false;
        other.host_visible_ = false;
    }
    return *this;
}

core::Result<GpuBuffer> GpuBuffer::create(const VkDevice device,
                                          const VkPhysicalDeviceMemoryProperties& properties,
                                          const GpuBufferDesc& desc) {
    if (device == VK_NULL_HANDLE) {
        return core::Result<GpuBuffer>::failure(core::ErrorCode::InvalidArgument,
                                                "GpuBuffer::create requires a valid VkDevice");
    }
    if (desc.size == 0) {
        return core::Result<GpuBuffer>::failure(core::ErrorCode::InvalidArgument,
                                                "GpuBuffer::create requires a non-zero size");
    }
    if (desc.usage == 0 || desc.memory_properties == 0) {
        return core::Result<GpuBuffer>::failure(core::ErrorCode::InvalidArgument,
                                                "GpuBuffer::create requires non-zero usage and memory properties");
    }
    if (desc.sharing_mode == VK_SHARING_MODE_CONCURRENT && desc.concurrent_families.size() < 2) {
        return core::Result<GpuBuffer>::failure(core::ErrorCode::InvalidArgument,
                                                "CONCURRENT buffers require at least two queue families");
    }

    VkBufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.size = desc.size;
    create_info.usage = desc.usage;
    create_info.sharingMode = desc.sharing_mode;
    if (desc.sharing_mode == VK_SHARING_MODE_CONCURRENT) {
        create_info.queueFamilyIndexCount = static_cast<u32>(desc.concurrent_families.size());
        create_info.pQueueFamilyIndices = desc.concurrent_families.data();
    }

    GpuBuffer buffer;
    buffer.device_ = device;
    buffer.size_ = desc.size;
    buffer.host_visible_ = (desc.memory_properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    buffer.coherent_ = (desc.memory_properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    if (const VkResult result = vkCreateBuffer(device, &create_info, nullptr, &buffer.buffer_);
        result != VK_SUCCESS) {
        return core::Result<GpuBuffer>::failure(core::ErrorCode::VulkanFailure,
                                                "vkCreateBuffer failed",
                                                static_cast<core::i32>(result));
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer.buffer_, &requirements);

    // Prefer the exact property set (e.g. HOST_VISIBLE|COHERENT). When that
    // fails and the caller asked for COHERENT, retry without it and fall back
    // to manual flushes (some mobile parts expose HOST_VISIBLE-only memory).
    u32 type = find_memory_type(properties, requirements.memoryTypeBits, desc.memory_properties);
    bool requires_manual_flush = false;
    if (type == ~u32{0} && buffer.coherent_) {
        const VkMemoryPropertyFlags relaxed = desc.memory_properties & ~static_cast<VkMemoryPropertyFlags>(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        type = find_memory_type(properties, requirements.memoryTypeBits, relaxed);
        requires_manual_flush = type != ~u32{0};
        if (requires_manual_flush) {
            buffer.coherent_ = false;
            AEGIS_LOG_WARN("[aegis] memory type lacks HOST_COHERENT; using manual flushes for a buffer");
        }
    }
    if (type == ~u32{0}) {
        buffer.reset();
        return core::Result<GpuBuffer>::failure(core::ErrorCode::Unavailable,
                                                "no Vulkan memory type matches the requested properties");
    }

    const VkMemoryAllocateInfo allocate_info{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = requirements.size,
        .memoryTypeIndex = type,
    };

    if (const VkResult result = vkAllocateMemory(device, &allocate_info, nullptr, &buffer.memory_);
        result != VK_SUCCESS) {
        buffer.reset();
        return core::Result<GpuBuffer>::failure(core::ErrorCode::OutOfMemory,
                                                "vkAllocateMemory failed",
                                                static_cast<core::i32>(result));
    }

    if (const VkResult result = vkBindBufferMemory(device, buffer.buffer_, buffer.memory_, 0);
        result != VK_SUCCESS) {
        buffer.reset();
        return core::Result<GpuBuffer>::failure(core::ErrorCode::VulkanFailure,
                                                "vkBindBufferMemory failed",
                                                static_cast<core::i32>(result));
    }

    if (buffer.host_visible_) {
        if (const VkResult result = vkMapMemory(device, buffer.memory_, 0, VK_WHOLE_SIZE, 0, &buffer.mapped_);
            result != VK_SUCCESS) {
            buffer.reset();
            AEGIS_LOG_WARN("[aegis] vkMapMemory failed for a host-visible buffer ("
                           << static_cast<int>(result) << ")");
            return core::Result<GpuBuffer>::failure(core::ErrorCode::VulkanFailure,
                                                    "vkMapMemory failed",
                                                    static_cast<core::i32>(result));
        }
    }

    return core::Result<GpuBuffer>::ok(std::move(buffer));
}

void GpuBuffer::write(const void* const data, const u64 offset, const u64 bytes) const {
    AEGIS_ASSERT_MSG(mapped_ != nullptr, "GpuBuffer::write requires a host-visible, mapped buffer");
    AEGIS_ASSERT_MSG(offset + bytes <= size_, "GpuBuffer::write range out of bounds");
    AEGIS_ASSERT_MSG(data != nullptr, "GpuBuffer::write data must not be null");

    std::memcpy(static_cast<char*>(mapped_) + offset, data, bytes);
    if (!coherent_) {
        flush();
    }
}

void GpuBuffer::flush() const {
    if (mapped_ == nullptr || coherent_) {
        return;
    }
    const VkMappedMemoryRange range{
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = memory_,
        .offset = 0,
        .size = VK_WHOLE_SIZE,
    };
    vkFlushMappedMemoryRanges(device_, 1, &range);
}

void GpuBuffer::reset() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (mapped_ != nullptr) {
        vkUnmapMemory(device_, memory_);
        mapped_ = nullptr;
    }
    if (buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer_, nullptr);
        buffer_ = VK_NULL_HANDLE;
    }
    if (memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory_, nullptr);
        memory_ = VK_NULL_HANDLE;
    }
    size_ = 0;
    coherent_ = false;
    host_visible_ = false;
    device_ = VK_NULL_HANDLE;
}

} // namespace aegis::memory
