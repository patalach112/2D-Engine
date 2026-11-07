#include <aegis/vk/instance.hpp>

#include <aegis/core/log.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace aegis::vk {

using core::u32;

namespace {

constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";

bool validation_layer_available() noexcept {
    u32 count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    std::vector<VkLayerProperties> layers(count);
    if (vkEnumerateInstanceLayerProperties(&count, layers.data()) != VK_SUCCESS) {
        return false;
    }
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, kValidationLayer) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

Instance::~Instance() { reset(); }

Instance::Instance(Instance&& other) noexcept
    : instance_(other.instance_), validation_(other.validation_) {
    other.instance_ = VK_NULL_HANDLE;
    other.validation_ = false;
}

Instance& Instance::operator=(Instance&& other) noexcept {
    if (this != &other) {
        reset();
        instance_ = other.instance_;
        validation_ = other.validation_;
        other.instance_ = VK_NULL_HANDLE;
        other.validation_ = false;
    }
    return *this;
}

core::Result<Instance> Instance::create(const InstanceConfig& config) {
    if (const VkResult init = volkInitialize(); init != VK_SUCCESS) {
        return core::Result<Instance>::failure(core::ErrorCode::Unavailable,
                                               "Vulkan loader (libvulkan.so.1) not available; "
                                               "install a Vulkan driver/loader",
                                               static_cast<core::i32>(init));
    }

    u32 api_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&api_version) != VK_SUCCESS ||
        VK_API_VERSION_MINOR(api_version) < 3) {
        return core::Result<Instance>::failure(
            core::ErrorCode::Unavailable,
            "Vulkan 1.3+ loader required (found API " + std::to_string(VK_API_VERSION_MAJOR(api_version)) +
                "." + std::to_string(VK_API_VERSION_MINOR(api_version)) + ")");
    }

    // Window-surface extensions returned by GLFW (VK_KHR_surface plus the
    // platform pair, e.g. VK_KHR_win32_surface on Windows) must reach
    // vkCreateInstance verbatim — glfwCreateWindowSurface resolves
    // vkCreateWin32SurfaceKHR against this list and fails with
    // VK_ERROR_EXTENSION_NOT_PRESENT otherwise. Copy them exactly, drop stray
    // nullptrs, and log what the instance is created with.
    std::vector<const char*> extensions = config.required_extensions;
    extensions.erase(std::remove(extensions.begin(), extensions.end(), nullptr), extensions.end());
    if (extensions.empty()) {
        AEGIS_LOG_WARN("[aegis] required_extensions is empty — the GLFW build likely has no Vulkan "
                       "windowing platform (Null platform fallback); window surface creation will fail");
    } else {
        for (const char* ext : extensions) {
            AEGIS_LOG_INFO("[aegis] enabling instance extension: " << ext);
        }
    }

    bool enable_validation = config.enable_validation;
    if (enable_validation && !validation_layer_available()) {
        AEGIS_LOG_WARN("[aegis] VK_LAYER_KHRONOS_validation requested but not installed; "
                       "continuing without validation");
        enable_validation = false;
    }

    const std::vector<const char*> layers = enable_validation
                                                ? std::vector<const char*>{kValidationLayer}
                                                : std::vector<const char*>{};

    const VkApplicationInfo app_info{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = config.application_name.c_str(),
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = config.engine_name.c_str(),
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = VK_API_VERSION_1_3,
    };

    const VkInstanceCreateInfo create_info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = static_cast<u32>(layers.size()),
        .ppEnabledLayerNames = layers.empty() ? nullptr : layers.data(),
        .enabledExtensionCount = static_cast<u32>(extensions.size()),
        .ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data(),
    };

    Instance instance;
    const VkResult result = vkCreateInstance(&create_info, nullptr, &instance.instance_);
    if (result != VK_SUCCESS) {
        return core::Result<Instance>::failure(core::ErrorCode::VulkanFailure,
                                               std::string("vkCreateInstance failed (loader may be "
                                                           "too old for Vulkan 1.3): ") +
                                                   std::to_string(static_cast<int>(result)),
                                               static_cast<core::i32>(result));
    }
    instance.validation_ = enable_validation;
    volkLoadInstance(instance.instance_);

    AEGIS_LOG_INFO("[aegis] Vulkan instance created (validation "
                   << (enable_validation ? "enabled" : "disabled") << ", API 1.3)");
    return core::Result<Instance>::ok(std::move(instance));
}

void Instance::reset() noexcept {
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    validation_ = false;
}

} // namespace aegis::vk
