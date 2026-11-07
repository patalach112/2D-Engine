#pragma once

namespace aegis::core {

/// Deleted copy construction/assignment; derived classes opt into move semantics
/// explicitly. Keeps non-owning handle types and RAII wrappers honest.
class NonCopyable {
public:
    NonCopyable() = default;
    ~NonCopyable() = default;
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

} // namespace aegis::core
