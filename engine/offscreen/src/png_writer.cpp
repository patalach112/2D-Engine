#include <aegis/offscreen/png_writer.hpp>

#include <miniz.h>

#include <fstream>
#include <string>
#include <vector>

namespace aegis::offscreen {

using core::u8;
using core::u32;
using core::u64;

namespace {

constexpr u8 kPngSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

void write_u32_be(std::vector<u8>& out, const u32 value) {
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>(value & 0xFFu));
}

void append_chunk(std::vector<u8>& out, const char type[4], const u8* data, const u64 data_size) {
    write_u32_be(out, static_cast<u32>(data_size));
    const u64 type_offset = out.size();
    out.insert(out.end(), type, type + 4);
    if (data != nullptr && data_size > 0) {
        out.insert(out.end(), data, data + data_size);
    }
    const u64 crc = mz_crc32(mz_crc32(0, nullptr, 0), out.data() + type_offset,
                             static_cast<size_t>(4 + data_size));
    write_u32_be(out, static_cast<u32>(crc));
}

}  // namespace

core::Result<void> write_png(const char* const path,
                             const u32 width,
                             const u32 height,
                             const u8* const rgba,
                             const u64 stride_bytes) {
    if (path == nullptr || rgba == nullptr || width == 0 || height == 0) {
        return core::Result<void>::failure(core::ErrorCode::InvalidArgument,
                                           "write_png requires a path, a pixel buffer and non-zero dimensions");
    }
    const u64 row_bytes = static_cast<u64>(width) * 4u;
    const u64 stride = stride_bytes == 0 ? row_bytes : stride_bytes;
    if (stride < row_bytes) {
        return core::Result<void>::failure(core::ErrorCode::InvalidArgument,
                                           "write_png: stride_bytes is smaller than a row");
    }

    // --- Build filter-0 scanlines (one filter byte per row) ------------------
    std::vector<u8> scanlines;
    scanlines.reserve(static_cast<u64>(height) * (row_bytes + 1));
    for (u32 row = 0; row < height; ++row) {
        scanlines.push_back(0);  // filter type 0 (None)
        const u8* src = rgba + static_cast<u64>(row) * stride;
        scanlines.insert(scanlines.end(), src, src + row_bytes);
    }

    // --- DEFLATE (zlib stream, as required by the PNG IDAT chunk) -------------
    std::vector<u8> compressed(static_cast<u64>(mz_compressBound(static_cast<mz_ulong>(scanlines.size()))));
    mz_ulong compressed_size = static_cast<mz_ulong>(compressed.size());
    const int compress_result = mz_compress(compressed.data(), &compressed_size, scanlines.data(),
                                            static_cast<mz_ulong>(scanlines.size()));
    if (compress_result != MZ_OK) {
        return core::Result<void>::failure(core::ErrorCode::IoError,
                                           "write_png: deflate failed (miniz code " +
                                               std::to_string(compress_result) + ")");
    }
    compressed.resize(static_cast<u64>(compressed_size));

    // --- Assemble the PNG container -------------------------------------------
    std::vector<u8> png;
    png.reserve(8 + 25 + 12 + compressed.size() + 12);
    png.insert(png.end(), kPngSignature, kPngSignature + sizeof(kPngSignature));

    u8 ihdr[13]{};
    const u32 be_width = width;
    const u32 be_height = height;
    ihdr[0] = static_cast<u8>((be_width >> 24) & 0xFFu);
    ihdr[1] = static_cast<u8>((be_width >> 16) & 0xFFu);
    ihdr[2] = static_cast<u8>((be_width >> 8) & 0xFFu);
    ihdr[3] = static_cast<u8>(be_width & 0xFFu);
    ihdr[4] = static_cast<u8>((be_height >> 24) & 0xFFu);
    ihdr[5] = static_cast<u8>((be_height >> 16) & 0xFFu);
    ihdr[6] = static_cast<u8>((be_height >> 8) & 0xFFu);
    ihdr[7] = static_cast<u8>(be_height & 0xFFu);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // color type 6 = RGBA
    ihdr[10] = 0; // compression method
    ihdr[11] = 0; // filter method
    ihdr[12] = 0; // interlace: none
    append_chunk(png, "IHDR", ihdr, sizeof(ihdr));

    append_chunk(png, "IDAT", compressed.data(), compressed.size());
    append_chunk(png, "IEND", nullptr, 0);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return core::Result<void>::failure(core::ErrorCode::IoError,
                                           std::string("write_png: cannot open '") + path + "' for writing");
    }
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file) {
        return core::Result<void>::failure(core::ErrorCode::IoError,
                                           std::string("write_png: failed to write '") + path + "'");
    }
    return core::Result<void>::ok();
}

}  // namespace aegis::offscreen
