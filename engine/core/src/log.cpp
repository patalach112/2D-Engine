#include <aegis/core/log.hpp>

#include <chrono>
#include <cstdio>
#include <ctime>

namespace aegis::core {

namespace {

const char* level_tag(const LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

std::string timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t seconds = clock::to_time_t(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    std::tm tm_snapshot{};
#if defined(_WIN32)
    localtime_s(&tm_snapshot, &seconds);
#else
    localtime_r(&seconds, &tm_snapshot);
#endif
    char buffer[16]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", tm_snapshot.tm_hour, tm_snapshot.tm_min,
                  tm_snapshot.tm_sec);
    char out[24]{};
    std::snprintf(out, sizeof(out), "%s.%03lld", buffer, static_cast<long long>(ms));
    return out;
}

} // namespace

void log(const LogLevel level, const std::string& message) noexcept {
    std::FILE* stream = (level == LogLevel::Error) ? stderr : stdout;
    std::fprintf(stream, "[%s] [%s] %s\n", timestamp().c_str(), level_tag(level), message.c_str());
    std::fflush(stream);
}

} // namespace aegis::core
