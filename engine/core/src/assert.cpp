#include <aegis/core/assert.hpp>

#include <cstdio>

namespace aegis {

void aegis_stderr(const std::string& message) noexcept {
    std::fputs(message.c_str(), stderr);
    std::fputc('\n', stderr);
}

namespace core {

void assertion_failed(const char* expression, const char* file, int line,
                      const char* message) noexcept {
    std::string out = "[aegis] ASSERT FAILED at ";
    out += file;
    out += ':';
    out += std::to_string(line);
    out += ": (";
    out += expression;
    out += ')';
    if (message != nullptr && *message != '\0') {
        out += " — ";
        out += message;
    }
    aegis_stderr(out);
    std::abort();
}

} // namespace core
} // namespace aegis
