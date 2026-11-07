// Aegis2D — minimal PNG writer (Milestone 5).
//
// Writes an RGBA8 image as a PNG file (no interlacing, filter type 0 per
// scanline). DEFLATE compression and CRC-32 come from miniz — no zlib
// dependency. RGB-type PNGs are intentionally not supported: the offscreen
// path renders RGBA8 and the writer matches that exactly.
#pragma once

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>

namespace aegis::offscreen {

/// Writes `width` x `height` RGBA8 pixels to `path`, overwriting the file.
/// `stride_bytes` is the source row pitch; 0 (default) means tightly packed
/// (width * 4). Pixels are written to the PNG exactly as provided (no color
/// space conversion). Returns InvalidArgument for empty images/buffers and
/// IoError on write/deflate failures.
[[nodiscard]] core::Result<void> write_png(const char* path,
                                           core::u32 width,
                                           core::u32 height,
                                           const core::u8* rgba,
                                           core::u64 stride_bytes = 0);

}  // namespace aegis::offscreen
