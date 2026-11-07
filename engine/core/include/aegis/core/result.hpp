// Aegis2D — lightweight Result<T> error handling.
//
// The engine does not use exceptions for control flow. Fallible operations
// return Result<T> (or Result<void>); callers must check has_value() before
// touching the payload. Accessing the payload of a failed Result is a fatal
// programming error (aborts via result_missing_value()).
#pragma once

#include <aegis/export.h>

#include <aegis/core/assert.hpp>
#include <aegis/core/types.hpp>

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace aegis::core {

enum class ErrorCode : u32 {
    Unknown = 0,
    InvalidArgument,
    NotFound,
    AlreadyExists,
    Unavailable,
    OutOfMemory,
    IoError,
    VulkanFailure,
};

AEGIS_API const char* error_code_name(ErrorCode code) noexcept;

struct AEGIS_API Error {
    ErrorCode code = ErrorCode::Unknown;
    i32 native_code = 0;  ///< platform/API-specific code (e.g. VkResult cast); 0 when N/A
    std::string message;

    static Error make(ErrorCode code, std::string message, i32 native_code = 0) {
        return Error{code, native_code, std::move(message)};
    }

    [[nodiscard]] std::string to_string() const {
        std::string out = error_code_name(code);
        if (native_code != 0) {
            out += " (native=" + std::to_string(native_code) + ")";
        }
        out += ": ";
        out += message;
        return out;
    }
};

/// Fatal misuse of a failed Result (payload accessed without a value).
[[noreturn]] AEGIS_API void result_missing_value() noexcept;

// ---------------------------------------------------------------------------
// Result<T> — either a payload T or an Error. Variant-backed, move-only-ish
// (copyable when T is, but intended to be used via moves/checks).
// ---------------------------------------------------------------------------
template <typename T>
class Result {
public:
    // NOLINTNEXTLINE(bugprone-unsafe-functions) — not unsafe, just banned by default rules
    Result(const Result&) = default;
    Result(Result&&) = default;  // NOLINT
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) = default;

    static Result ok(T value) { return Result(std::in_place_type<T>, std::move(value)); }
    static Result failure(Error error) { return Result(std::in_place_type<Error>, std::move(error)); }
    static Result failure(ErrorCode code, std::string message, i32 native_code = 0) {
        return failure(Error::make(code, std::move(message), native_code));
    }

    [[nodiscard]] bool has_value() const noexcept { return std::holds_alternative<T>(storage_); }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& value() & {
        if (!has_value()) {
            result_missing_value();
        }
        return std::get<T>(storage_);
    }
    [[nodiscard]] const T& value() const& {
        if (!has_value()) {
            result_missing_value();
        }
        return std::get<T>(storage_);
    }
    [[nodiscard]] T&& value() && {
        if (!has_value()) {
            result_missing_value();
        }
        return std::get<T>(std::move(storage_));
    }

    /// Move the payload out of a successful result (fatal on failure).
    [[nodiscard]] T take() && {
        if (!has_value()) {
            result_missing_value();
        }
        return std::move(std::get<T>(storage_));
    }

    [[nodiscard]] Error& error() & {
        AEGIS_ASSERT(!has_value() && "error() called on a successful Result");
        return std::get<Error>(storage_);
    }
    [[nodiscard]] const Error& error() const& {
        AEGIS_ASSERT(!has_value() && "error() called on a successful Result");
        return std::get<Error>(storage_);
    }

private:
    template <typename U, typename... Args>
    Result(std::in_place_type_t<U>, Args&&... args) : storage_(std::in_place_type<U>, std::forward<Args>(args)...) {}

    std::variant<T, Error> storage_;
};

// ---------------------------------------------------------------------------
// Result<void> — success or Error, no payload.
// ---------------------------------------------------------------------------
template <>
class Result<void> {
public:
    static Result ok() { return Result(true, Error{}); }
    static Result failure(Error error) { return Result(false, std::move(error)); }
    static Result failure(ErrorCode code, std::string message, i32 native_code = 0) {
        return Result(false, Error::make(code, std::move(message), native_code));
    }

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    explicit operator bool() const noexcept { return ok_; }

    [[nodiscard]] Error& error() & {
        AEGIS_ASSERT(!ok_ && "error() called on a successful Result<void>");
        return error_;
    }
    [[nodiscard]] const Error& error() const& {
        AEGIS_ASSERT(!ok_ && "error() called on a successful Result<void>");
        return error_;
    }

private:
    Result(bool ok, Error error) : ok_(ok), error_(std::move(error)) {}

    bool ok_ = true;
    Error error_{};
};

} // namespace aegis::core
