#pragma once

#include <aegis/core/types.hpp>

#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>

namespace aegis::core {

// ---------------------------------------------------------------------------
// Minimal logging: line-oriented, timestamped, level-tagged, no dependencies.
// Usage: AEGIS_LOG_INFO("answer=" << 42);
// ---------------------------------------------------------------------------
enum class LogLevel : u32 { Debug = 0, Info, Warn, Error };

void log(LogLevel level, const std::string& message) noexcept;

namespace detail {

class LogStream {
public:
    explicit LogStream(LogLevel level) : level_(level) {}
    ~LogStream() { log(level_, oss_.str()); }

    template <typename T>
    LogStream& operator<<(const T& value) {
        oss_ << value;
        return *this;
    }

private:
    LogLevel level_;
    std::ostringstream oss_;
};

} // namespace detail

} // namespace aegis::core

#define AEGIS_LOG_DEBUG(msg) \
    do { ::aegis::core::detail::LogStream(::aegis::core::LogLevel::Debug) << msg; } while (0)
#define AEGIS_LOG_INFO(msg) \
    do { ::aegis::core::detail::LogStream(::aegis::core::LogLevel::Info) << msg; } while (0)
#define AEGIS_LOG_WARN(msg) \
    do { ::aegis::core::detail::LogStream(::aegis::core::LogLevel::Warn) << msg; } while (0)
#define AEGIS_LOG_ERROR(msg) \
    do { ::aegis::core::detail::LogStream(::aegis::core::LogLevel::Error) << msg; } while (0)
