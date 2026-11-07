#include <aegis/core/result.hpp>

namespace aegis::core {

const char* error_code_name(const ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Unknown:         return "Unknown";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::NotFound:        return "NotFound";
        case ErrorCode::AlreadyExists:   return "AlreadyExists";
        case ErrorCode::Unavailable:     return "Unavailable";
        case ErrorCode::OutOfMemory:     return "OutOfMemory";
        case ErrorCode::IoError:         return "IoError";
        case ErrorCode::VulkanFailure:   return "VulkanFailure";
    }
    return "Unknown";
}

void result_missing_value() noexcept {
    aegis_stderr("[aegis] FATAL: Result payload accessed while holding an error (use has_value())");
    std::abort();
}

} // namespace aegis::core
