// Aegis2D — Vulkan instance management (loader bootstrap via volk).
#pragma once

#include <volk.h>

#include <aegis/core/noncopyable.hpp>
#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>

#include <string>
#include <vector>

namespace aegis::vk {

struct InstanceConfig {
    std::string application_name = "Aegis2D";
    std::string engine_name = "Aegis2D";
    /// Enable VK_LAYER_KHRONOS_validation when it is installed. Missing layer
    /// degrades gracefully to a warning (never an error).
    bool enable_validation = true;
    /// Instance-level extensions the application needs (e.g. the two GLFW
    /// surface extensions). vkCreateInstance fails if these are missing.
    std::vector<const char*> required_extensions;
};

class Instance : public core::NonCopyable {
public:
    Instance() = default;
    ~Instance();
    Instance(Instance&& other) noexcept;
    Instance& operator=(Instance&& other) noexcept;

    /// volkInitialize() + version check (Vulkan 1.3+ loader mandatory) + instance
    /// creation. On success the instance-level function pointers are loaded via
    /// volkLoadInstance().
    [[nodiscard]] static core::Result<Instance> create(const InstanceConfig& config);

    VkInstance handle() const noexcept { return instance_; }
    bool validation_enabled() const noexcept { return validation_; }

private:
    void reset() noexcept;

    VkInstance instance_ = VK_NULL_HANDLE;
    bool validation_ = false;
};

} // namespace aegis::vk
