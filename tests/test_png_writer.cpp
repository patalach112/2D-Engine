// Aegis2D — unit tests for the PNG writer (Milestone 5).
// Verifies the full write -> parse -> inflate -> compare round trip, without
// requiring a Vulkan driver or GPU.
#include <aegis/offscreen/png_writer.hpp>

#include <miniz.h>

#include "aegis_test.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <iterator>
#include <vector>

using aegis::core::u8;
using aegis::core::u32;
using aegis::core::u64;

namespace {

constexpr u8 kSignature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

struct PngChunk {
    char type[5] = {};
    std::vector<u8> data;
};

u32 read_u32_be(const std::vector<u8>& bytes, const u64 offset) {
    return (static_cast<u32>(bytes[offset]) << 24) | (static_cast<u32>(bytes[offset + 1]) << 16) |
           (static_cast<u32>(bytes[offset + 2]) << 8) | static_cast<u32>(bytes[offset + 3]);
}

std::vector<PngChunk> parse_chunks(const std::vector<u8>& png, bool& crc_ok) {
    std::vector<PngChunk> chunks;
    crc_ok = true;
    u64 offset = 8;  // skip signature
    while (offset + 8 <= png.size()) {
        const u32 length = read_u32_be(png, offset);
        const u64 data_begin = offset + 8;
        if (data_begin + length + 4 > png.size()) {
            crc_ok = false;
            break;
        }
        PngChunk chunk;
        std::memcpy(chunk.type, png.data() + offset + 4, 4);
        chunk.type[4] = '\0';
        chunk.data.assign(png.begin() + static_cast<std::ptrdiff_t>(data_begin),
                          png.begin() + static_cast<std::ptrdiff_t>(data_begin + length));

        const u32 expected_crc = read_u32_be(png, data_begin + length);
        const u64 actual_crc = mz_crc32(mz_crc32(0, nullptr, 0), png.data() + offset + 4,
                                        static_cast<size_t>(length + 4));
        if (static_cast<u32>(actual_crc) != expected_crc) {
            crc_ok = false;
        }

        chunks.push_back(std::move(chunk));
        offset = data_begin + length + 4;
        if (std::string(chunks.back().type) == "IEND") {
            break;
        }
    }
    return chunks;
}

std::vector<u8> read_file_bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

}  // namespace

AEGIS_TEST(png_writer, "round trip: written PNG decodes to the exact source bytes") {
    constexpr u32 width = 48;
    constexpr u32 height = 24;

    // Deterministic pixel pattern (includes a strong gradient + noise mix).
    std::vector<u8> pixels(static_cast<u64>(width) * height * 4u);
    for (u64 i = 0; i < pixels.size(); i += 4) {
        const u32 pixel_index = static_cast<u32>(i / 4);
        pixels[i + 0] = static_cast<u8>((pixel_index * 7U + 31U) & 0xFFu);
        pixels[i + 1] = static_cast<u8>((pixel_index * 13U + 101U) & 0xFFu);
        pixels[i + 2] = static_cast<u8>((pixel_index * 29U + 211U) & 0xFFu);
        pixels[i + 3] = static_cast<u8>(160U + ((pixel_index * 11U) & 0x5Fu));
    }

    const auto path = std::filesystem::temp_directory_path() / "aegis_test_roundtrip.png";
    const auto write_result = aegis::offscreen::write_png(path.string().c_str(), width, height, pixels.data());
    AEGIS_REQUIRE(write_result.has_value());

    const std::vector<u8> png = read_file_bytes(path);
    std::filesystem::remove(path);

    AEGIS_REQUIRE(png.size() > 8u + 25u + 12u + 12u);
    AEGIS_CHECK(std::memcmp(png.data(), kSignature, sizeof(kSignature)) == 0);

    bool crc_ok = false;
    const std::vector<PngChunk> chunks = parse_chunks(png, crc_ok);
    AEGIS_CHECK(crc_ok);
    AEGIS_REQUIRE(chunks.size() == 3u);
    AEGIS_CHECK_EQ(std::string(chunks[0].type), std::string("IHDR"));
    AEGIS_CHECK_EQ(std::string(chunks[1].type), std::string("IDAT"));
    AEGIS_CHECK_EQ(std::string(chunks[2].type), std::string("IEND"));

    // IHDR: width, height, bit depth 8, color type 6 (RGBA), methods 0.
    AEGIS_REQUIRE(chunks[0].data.size() == 13u);
    AEGIS_CHECK_EQ(read_u32_be(chunks[0].data, 0), width);
    AEGIS_CHECK_EQ(read_u32_be(chunks[0].data, 4), height);
    AEGIS_CHECK_EQ(chunks[0].data[8], u8{8});
    AEGIS_CHECK_EQ(chunks[0].data[9], u8{6});
    AEGIS_CHECK_EQ(chunks[0].data[12], u8{0});

    // IDAT: inflate to the filter-0 scanlines and compare byte-for-byte.
    const u64 expected_raw_size = static_cast<u64>(height) * (static_cast<u64>(width) * 4u + 1u);
    std::vector<u8> raw(expected_raw_size);
    mz_ulong raw_size = static_cast<mz_ulong>(raw.size());
    const int inflate_result =
        mz_uncompress(raw.data(), &raw_size, chunks[1].data.data(),
                      static_cast<mz_ulong>(chunks[1].data.size()));
    AEGIS_REQUIRE(inflate_result == MZ_OK);
    AEGIS_CHECK_EQ(static_cast<u64>(raw_size), expected_raw_size);

    u64 src = 0;
    for (u32 row = 0; row < height; ++row) {
        const u64 row_begin = static_cast<u64>(row) * (static_cast<u64>(width) * 4u + 1u);
        AEGIS_CHECK_EQ(raw[row_begin], u8{0});  // filter type None
        const u64 row_bytes = static_cast<u64>(width) * 4u;
        AEGIS_CHECK(std::memcmp(raw.data() + row_begin + 1, pixels.data() + src, row_bytes) == 0);
        src += row_bytes;
    }
}

AEGIS_TEST(png_writer, "writer honours a custom source stride") {
    constexpr u32 width = 8;
    constexpr u32 height = 4;
    constexpr u64 stride = static_cast<u64>(width) * 4u + 12u;  // 12 bytes of padding per row

    std::vector<u8> pixels(static_cast<u64>(height) * stride, 0xAB);
    for (u64 row = 0; row < height; ++row) {
        for (u64 col = 0; col < static_cast<u64>(width) * 4u; ++col) {
            pixels[row * stride + col] = static_cast<u8>(row * 16u + col);
        }
    }

    const auto path = std::filesystem::temp_directory_path() / "aegis_test_stride.png";
    const auto write_result =
        aegis::offscreen::write_png(path.string().c_str(), width, height, pixels.data(), stride);
    AEGIS_REQUIRE(write_result.has_value());

    const std::vector<u8> png = read_file_bytes(path);
    std::filesystem::remove(path);

    bool crc_ok = false;
    const std::vector<PngChunk> chunks = parse_chunks(png, crc_ok);
    AEGIS_REQUIRE(chunks.size() == 3u);

    const u64 expected_raw_size = static_cast<u64>(height) * (static_cast<u64>(width) * 4u + 1u);
    std::vector<u8> raw(expected_raw_size);
    mz_ulong raw_size = static_cast<mz_ulong>(raw.size());
    AEGIS_REQUIRE(mz_uncompress(raw.data(), &raw_size, chunks[1].data.data(),
                                static_cast<mz_ulong>(chunks[1].data.size())) == MZ_OK);

    u64 src = 0;
    for (u32 row = 0; row < height; ++row) {
        const u64 row_begin = static_cast<u64>(row) * (static_cast<u64>(width) * 4u + 1u);
        AEGIS_CHECK(std::memcmp(raw.data() + row_begin + 1, pixels.data() + src,
                                static_cast<u64>(width) * 4u) == 0);
        src += stride;
    }
}

AEGIS_TEST(png_writer, "invalid arguments are rejected without writing") {
    std::vector<u8> pixels(64, 0);
    const auto result = aegis::offscreen::write_png("/tmp/aegis_should_not_exist.png", 0, 0, pixels.data());
    AEGIS_CHECK(!result.has_value());

    const auto null_pixels = aegis::offscreen::write_png("/tmp/aegis_should_not_exist.png", 4, 4, nullptr);
    AEGIS_CHECK(!null_pixels.has_value());
}
