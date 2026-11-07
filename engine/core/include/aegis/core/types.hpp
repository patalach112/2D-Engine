#pragma once

#include <cstdint>

namespace aegis::core {

using f32 = float;
using f64 = double;

using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

static_assert(sizeof(f32) == 4 && sizeof(f64) == 8);
static_assert(sizeof(i32) == 4 && sizeof(i64) == 8);
static_assert(sizeof(u32) == 4 && sizeof(u64) == 8);

} // namespace aegis::core
