#pragma once

#include <aegis/core/types.hpp>

namespace aegis::memory {

/// Default alignment for arena/staging allocations (covers vertex+index SIMD
/// requirements and minUniformBufferOffsetAlignment on virtually all GPUs).
inline constexpr core::u64 kDefaultAlignment = 256;

} // namespace aegis::memory
