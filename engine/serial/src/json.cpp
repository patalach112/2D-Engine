// Aegis2D — strict JSON subset, implementation (Milestone 10).
#include <aegis/serial/json.hpp>

#include <aegis/core/version.hpp>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aegis::serial {

const Json* Json::find(const std::string_view key) const noexcept {
    if (kind_ != Kind::Object) {
        return nullptr;
    }
    for (const Member& member : obj_) {
        if (member.first == key) {
            return &member.second;
        }
    }
    return nullptr;
}

bool Json::is_integral() const noexcept {
    if (kind_ != Kind::Number) {
        return false;
    }
    if (!std::isfinite(num_)) {
        return false;
    }
    const double truncated = std::trunc(num_);
    if (truncated != num_) {
        return false;
    }
    return std::fabs(num_) <= 9007199254740992.0;  // 2^53
}

long long Json::as_integral() const noexcept { return static_cast<long long>(num_); }

namespace {

constexpr std::size_t kMaxBytes = 1u << 20;  // 1 MiB document cap
constexpr core::u32 kMaxDepth = 64;

struct Parser {
    std::string_view text;
    std::size_t pos = 0;
    core::u32 line = 1;
    core::u32 col = 1;

    // --- plumbing -----------------------------------------------------------
    [[nodiscard]] core::Result<Json> fail(const char* message) const {
        return core::Result<Json>::failure(core::ErrorCode::InvalidArgument, fmt_error(message));
    }
    [[nodiscard]] core::Result<std::string> fail_string(const char* message) const {
        return core::Result<std::string>::failure(core::ErrorCode::InvalidArgument,
                                                  fmt_error(message));
    }
    [[nodiscard]] core::Result<std::uint32_t> fail_u32(const char* message) const {
        return core::Result<std::uint32_t>::failure(core::ErrorCode::InvalidArgument,
                                                    fmt_error(message));
    }
    [[nodiscard]] std::string fmt_error(const char* message) const {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "json: %s (line %u, column %u)", message, line, col);
        return std::string(buffer);
    }

    [[nodiscard]] bool eof() const noexcept { return pos >= text.size(); }
    [[nodiscard]] char peek() const noexcept { return eof() ? '\0' : text[pos]; }
    char advance() noexcept {
        const char c = text[pos++];
        if (c == '\n') {
            ++line;
            col = 1;
        } else {
            ++col;
        }
        return c;
    }
    void skip_ws() noexcept {
        while (!eof()) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                advance();
            } else {
                break;
            }
        }
    }
    [[nodiscard]] bool consume(const char expected) noexcept {
        if (peek() != expected) {
            return false;
        }
        advance();
        return true;
    }

    // --- values -------------------------------------------------------------
    [[nodiscard]] core::Result<Json> parse() {
        if (text.size() > kMaxBytes) {
            return fail("document exceeds the 1 MiB cap");
        }
        skip_ws();
        auto value = parse_value(0);
        if (!value) {
            return value;
        }
        skip_ws();
        if (!eof()) {
            return fail("trailing content after JSON value");
        }
        return value;
    }

    [[nodiscard]] core::Result<Json> parse_value(const core::u32 depth) {
        if (depth > kMaxDepth) {
            return fail("nesting too deep");
        }
        skip_ws();
        switch (peek()) {
            case '{': return parse_object(depth + 1);
            case '[': return parse_array(depth + 1);  // children parse at depth+1
            case '"': return parse_string_value();
            case 't': return parse_literal("true", Json(true));
            case 'f': return parse_literal("false", Json(false));
            case 'n': return parse_literal("null", Json());
            case '-':
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9': return parse_number();
            default: return fail("unexpected character");
        }
    }

    [[nodiscard]] core::Result<Json> parse_literal(const char* literal, Json value) {
        const std::size_t length = std::strlen(literal);
        if (text.substr(pos, length) != literal) {
            return fail("invalid literal");
        }
        for (std::size_t i = 0; i < length; ++i) {
            advance();
        }
        return core::Result<Json>::ok(std::move(value));
    }

    [[nodiscard]] core::Result<Json> parse_string_value() {
        auto string = parse_string();
        if (!string) {
            return core::Result<Json>::failure(string.error().code, string.error().message);
        }
        return core::Result<Json>::ok(Json(std::move(string).take()));
    }

    [[nodiscard]] core::Result<std::string> parse_string() {
        if (!consume('"')) {
            return fail_string("expected '\"'");
        }
        std::string out;
        while (true) {
            if (eof()) {
                return fail_string("unterminated string");
            }
            const char c = advance();
            if (c == '"') {
                return core::Result<std::string>::ok(std::move(out));
            }
            if (c == '\\') {
                auto escaped = parse_escape();
                if (!escaped) {
                    return escaped;  // Result<std::string> — propagates as-is
                }
                out += std::move(escaped).take();
                continue;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return fail_string("raw control character in string");
            }
            out += c;
        }
    }

    [[nodiscard]] core::Result<std::string> parse_escape() {
        if (eof()) {
            return fail_string("truncated escape sequence");
        }
        const char c = advance();
        switch (c) {
            case '"': return core::Result<std::string>::ok("\"");
            case '\\': return core::Result<std::string>::ok("\\");
            case '/': return core::Result<std::string>::ok("/");
            case 'b': return core::Result<std::string>::ok("\b");
            case 'f': return core::Result<std::string>::ok("\f");
            case 'n': return core::Result<std::string>::ok("\n");
            case 'r': return core::Result<std::string>::ok("\r");
            case 't': return core::Result<std::string>::ok("\t");
            case 'u': {
                auto first = parse_hex4();
                if (!first) {
                    return core::Result<std::string>::failure(first.error().code, first.error().message);
                }
                std::uint32_t codepoint = std::move(first).take();
                if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                    // High surrogate — must be followed by \uYYYY low surrogate.
                    const std::size_t save_pos = pos;
                    const core::u32 save_line = line, save_col = col;
                    if (consume('\\') && consume('u')) {
                        auto second = parse_hex4();
                        if (second) {
                            const std::uint32_t low = std::move(second).take();
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low - 0xDC00));
                                return core::Result<std::string>::ok(encode_utf8(codepoint));
                            }
                        }
                    }
                    pos = save_pos;
                    line = save_line;
                    col = save_col;
                    return fail_string("lone high surrogate in \\u escape");
                }
                if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
                    return fail_string("lone low surrogate in \\u escape");
                }
                return core::Result<std::string>::ok(encode_utf8(codepoint));
            }
            default: return fail_string("invalid escape sequence");
        }
    }

    [[nodiscard]] core::Result<std::uint32_t> parse_hex4() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) {
                return fail_u32("truncated \\u escape");
            }
            const char c = advance();
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return fail_u32("invalid hex digit in \\u escape");
            }
            value = (value << 4) | digit;
        }
        return core::Result<std::uint32_t>::ok(value);
    }

    [[nodiscard]] static std::string encode_utf8(const std::uint32_t codepoint) {
        std::string out;
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0 | (codepoint >> 6));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            out += static_cast<char>(0xE0 | (codepoint >> 12));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (codepoint >> 18));
            out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        return out;
    }

    [[nodiscard]] core::Result<Json> parse_number() {
        const std::size_t begin = pos;
        if (consume('-') && eof()) {
            return fail("truncated number");
        }
        if (peek() == '0') {
            advance();
        } else if (peek() >= '1' && peek() <= '9') {
            while (!eof() && peek() >= '0' && peek() <= '9') {
                advance();
            }
        } else {
            return fail("invalid number");
        }
        if (peek() == '.') {
            advance();
            if (eof() || peek() < '0' || peek() > '9') {
                return fail("fraction needs at least one digit");
            }
            while (!eof() && peek() >= '0' && peek() <= '9') {
                advance();
            }
        }
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            if (eof() || peek() < '0' || peek() > '9') {
                return fail("exponent needs at least one digit");
            }
            while (!eof() && peek() >= '0' && peek() <= '9') {
                advance();
            }
        }
        const std::string_view slice = text.substr(begin, pos - begin);
        double value = 0.0;
        const auto [ptr, ec] = std::from_chars(slice.data(), slice.data() + slice.size(), value);
        if (ec != std::errc() || ptr != slice.data() + slice.size()) {
            return fail("number out of range");
        }
        return core::Result<Json>::ok(Json(value));
    }

    [[nodiscard]] core::Result<Json> parse_array(const core::u32 depth) {
        if (!consume('[')) {
            return fail("expected '['");
        }
        Json array = Json::make_array();
        skip_ws();
        if (consume(']')) {
            return core::Result<Json>::ok(std::move(array));
        }
        while (true) {
            auto element = parse_value(depth);
            if (!element) {
                return element;
            }
            array.push_back(std::move(element).take());
            skip_ws();
            if (consume(']')) {
                return core::Result<Json>::ok(std::move(array));
            }
            if (!consume(',')) {
                return fail("expected ',' or ']' in array");
            }
        }
    }

    [[nodiscard]] core::Result<Json> parse_object(const core::u32 depth) {
        if (!consume('{')) {
            return fail("expected '{'");
        }
        Json object = Json::make_object();
        auto add_member = [&object](std::string key, Json value) -> bool {
            for (const Json::Member& member : object.as_object()) {
                if (member.first == key) {
                    return false;  // duplicate key — rejected
                }
            }
            object.set(std::move(key), std::move(value));
            return true;
        };
        skip_ws();
        if (consume('}')) {
            return core::Result<Json>::ok(std::move(object));
        }
        while (true) {
            skip_ws();
            if (peek() != '"') {
                return fail("object keys must be strings");
            }
            auto key = parse_string();
            if (!key) {
                return core::Result<Json>::failure(key.error().code, key.error().message);
            }
            skip_ws();
            if (!consume(':')) {
                return fail("expected ':' after object key");
            }
            auto value = parse_value(depth);
            if (!value) {
                return value;
            }
            if (!add_member(std::move(key).take(), std::move(value).take())) {
                return fail("duplicate object key");
            }
            skip_ws();
            if (consume('}')) {
                return core::Result<Json>::ok(std::move(object));
            }
            if (!consume(',')) {
                return fail("expected ',' or '}' in object");
            }
        }
    }
};

void write_escaped(std::string& out, const std::string& text) {
    out += '"';
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                    out += buffer;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void write_number(std::string& out, const double value) {
    if (std::isfinite(value) && value == std::trunc(value) && std::fabs(value) <= 9007199254740992.0) {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        out += buffer;
        return;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    out += buffer;
}

void write_value(std::string& out, const Json& value, const int indent) {
    const std::string padding(static_cast<std::size_t>(indent) * 2, ' ');
    const std::string child_padding(static_cast<std::size_t>(indent + 1) * 2, ' ');
    switch (value.kind()) {
        case Json::Kind::Null: out += "null"; return;
        case Json::Kind::Bool: out += value.as_bool() ? "true" : "false"; return;
        case Json::Kind::Number: write_number(out, value.as_number()); return;
        case Json::Kind::String: write_escaped(out, value.as_string()); return;
        case Json::Kind::Array: {
            if (value.as_array().empty()) {
                out += "[]";
                return;
            }
            out += "[\n";
            bool first = true;
            for (const Json& element : value.as_array()) {
                if (!first) {
                    out += ",\n";
                }
                first = false;
                out += child_padding;
                write_value(out, element, indent + 1);
            }
            out += "\n";
            out += padding;
            out += "]";
            return;
        }
        case Json::Kind::Object: {
            if (value.as_object().empty()) {
                out += "{}";
                return;
            }
            out += "{\n";
            bool first = true;
            for (const auto& [key, member] : value.as_object()) {
                if (!first) {
                    out += ",\n";
                }
                first = false;
                out += child_padding;
                write_escaped(out, key);
                out += ": ";
                write_value(out, member, indent + 1);
            }
            out += "\n";
            out += padding;
            out += "}";
            return;
        }
    }
}

}  // namespace

core::Result<Json> parse_json(const std::string_view text) noexcept {
    if (text.size() > kMaxBytes) {
        return core::Result<Json>::failure(core::ErrorCode::InvalidArgument,
                                           "json: document exceeds the 1 MiB cap");
    }
    Parser parser{text};
    return parser.parse();
}

void write_json(std::string& out, const Json& value) noexcept {
    try {
        write_value(out, value, 0);
    } catch (...) {
        // std::string growth can throw; leave `out` with whatever it held.
    }
}

std::string to_json(const Json& value) noexcept {
    std::string out;
    write_json(out, value);
    return out;
}

}  // namespace aegis::serial
