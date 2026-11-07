#pragma once

#include <aegis/export.h>

#include <aegis/core/types.hpp>

#include <cstdlib>
#include <string>

namespace aegis {

/// Prints a plain, unannotated diagnostic line to stderr. Used by the assert
/// and result machinery before anything else is guaranteed to be usable.
AEGIS_API void aegis_stderr(const std::string& message) noexcept;

namespace core {

/// Fatal assertion. On failure: prints file:line + expression to stderr and
/// aborts. Always enabled (the engine does not compile asserts out); checked
/// on every build configuration so misuse fails fast in development and in
/// release equally.
AEGIS_API void assertion_failed(const char* expression, const char* file, int line, const char* message) noexcept;

} // namespace core
} // namespace aegis

#define AEGIS_ASSERT(cond)                                                                  \
    do {                                                                                    \
        if (!static_cast<bool>(cond)) {                                                     \
            ::aegis::core::assertion_failed(#cond, __FILE__, __LINE__, nullptr);            \
        }                                                                                   \
    } while (0)

#define AEGIS_ASSERT_MSG(cond, msg)                                                        \
    do {                                                                                    \
        if (!static_cast<bool>(cond)) {                                                     \
            ::aegis::core::assertion_failed(#cond, __FILE__, __LINE__, (msg));              \
        }                                                                                   \
    } while (0)
