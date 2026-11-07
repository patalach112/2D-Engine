// Aegis2D — minimal, strict JSON subset (Milestone 10).
//
// Hand-rolled on purpose: the engine ships zero third-party dependencies, so
// craft files cannot rely on nlohmann/json or similar. This parser accepts
// exactly the JSON data model (objects, arrays, strings, numbers, booleans,
// null) — no comments, no trailing commas, no NaN/Infinity, no duplicate
// object keys, no leading zeros — and rejects everything else with a
// line/column error. It is deliberately not a general JSON5/JS engine.
//
// Limits (defense against hostile or corrupt files):
//   * document size <= 1 MiB
//   * nesting depth  <= 64
//   * array/object fan-out is bounded by the size cap itself
//
// Numbers are held as f64. Integer-valued numbers within +/-2^53 round-trip
// exactly; integral values are printed without a fraction so saved files stay
// tidy and diff-friendly.
#pragma once

#include <aegis/export.h>

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace aegis::serial {

class AEGIS_API Json {
public:
    enum class Kind : core::u8 { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;  ///< insertion order; craft output is deterministic

    Json() noexcept = default;  // Null
    Json(const bool v) noexcept : kind_(Kind::Bool), bool_(v) {}
    Json(const double v) noexcept : kind_(Kind::Number), num_(v) {}
    Json(const int v) noexcept : kind_(Kind::Number), num_(static_cast<double>(v)) {}
    Json(const long long v) noexcept : kind_(Kind::Number), num_(static_cast<double>(v)) {}
    Json(std::string v) : kind_(Kind::String), str_(std::move(v)) {}
    Json(const char* v) : kind_(Kind::String), str_(v) {}

    [[nodiscard]] static Json make_array() noexcept {
        Json j;
        j.kind_ = Kind::Array;
        return j;
    }
    [[nodiscard]] static Json make_object() noexcept {
        Json j;
        j.kind_ = Kind::Object;
        return j;
    }

    void push_back(Json value) noexcept(false) { as_array().push_back(std::move(value)); }
    void set(std::string key, Json value) noexcept(false) {
        auto& members = as_object();
        for (auto& member : members) {
            if (member.first == key) {  // replace in place — no duplicate keys
                member.second = std::move(value);
                return;
            }
        }
        members.push_back({std::move(key), std::move(value)});
    }

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool is_null() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool is_bool() const noexcept { return kind_ == Kind::Bool; }
    [[nodiscard]] bool is_number() const noexcept { return kind_ == Kind::Number; }
    [[nodiscard]] bool is_string() const noexcept { return kind_ == Kind::String; }
    [[nodiscard]] bool is_array() const noexcept { return kind_ == Kind::Array; }
    [[nodiscard]] bool is_object() const noexcept { return kind_ == Kind::Object; }

    [[nodiscard]] bool as_bool() const noexcept { return bool_; }
    [[nodiscard]] double as_number() const noexcept { return num_; }
    [[nodiscard]] const std::string& as_string() const noexcept { return str_; }
    [[nodiscard]] const Array& as_array() const noexcept { return arr_; }
    [[nodiscard]] const Object& as_object() const noexcept { return obj_; }
    Array& as_array() noexcept { return arr_; }
    Object& as_object() noexcept { return obj_; }

    /// Object member or nullptr when absent / not an object. Linear scan is
    /// intentional — craft documents have at most a few hundred members.
    [[nodiscard]] const Json* find(std::string_view key) const noexcept;

    /// True when the number is an exact integer within +/-2^53.
    [[nodiscard]] bool is_integral() const noexcept;
    /// Exact integer value after is_integral() succeeds.
    [[nodiscard]] long long as_integral() const noexcept;

private:
    Kind kind_ = Kind::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    Array arr_;
    Object obj_;
};

/// Parses a JSON document. On failure the error message carries line:column.
[[nodiscard]] AEGIS_API core::Result<Json> parse_json(std::string_view text) noexcept;

/// Writes deterministic 2-space-indented JSON (object member order preserved).
AEGIS_API void write_json(std::string& out, const Json& value) noexcept;

/// Convenience wrapper around write_json.
[[nodiscard]] AEGIS_API std::string to_json(const Json& value) noexcept;

}  // namespace aegis::serial
