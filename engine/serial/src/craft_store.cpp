// Aegis2D — craft slot persistence, implementation (Milestone 10).
#include <aegis/serial/craft_store.hpp>

#include <aegis/serial/craft.hpp>
#include <aegis/serial/json.hpp>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace aegis::serial {

std::filesystem::path default_craft_dir() noexcept {
    if (const char* override_dir = std::getenv("AEGIS_CRAFT_DIR")) {
        if (override_dir[0] != '\0') {
            return std::filesystem::path(override_dir);
        }
    }
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA")) {
        return std::filesystem::path(appdata) / "aegis2d" / "craft";
    }
    if (const char* userprofile = std::getenv("USERPROFILE")) {
        return std::filesystem::path(userprofile) / "AppData" / "Roaming" / "aegis2d" / "craft";
    }
    return std::filesystem::path("aegis2d-craft");
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "aegis2d" / "craft";
    }
    return std::filesystem::path("aegis2d-craft");
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
        if (xdg[0] != '\0') {
            return std::filesystem::path(xdg) / "aegis2d" / "craft";
        }
    }
    if (const char* home = std::getenv("HOME")) {
        return std::filesystem::path(home) / ".local" / "share" / "aegis2d" / "craft";
    }
    return std::filesystem::path("aegis2d-craft");
#endif
}

core::Result<std::filesystem::path> ensure_craft_dir(const std::filesystem::path& dir) noexcept {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return core::Result<std::filesystem::path>::failure(
            core::ErrorCode::Unknown,
            std::string("craft: cannot create directory '") + dir.string() + "': " + ec.message());
    }
    return core::Result<std::filesystem::path>::ok(dir);
}

std::filesystem::path craft_slot_path(const std::filesystem::path& dir, const core::u32 slot) noexcept {
    return dir / ("craft_" + std::to_string(slot) + ".aegiscraft");
}

std::vector<CraftSlotInfo> list_craft_slots(const std::filesystem::path& dir) noexcept {
    std::vector<CraftSlotInfo> slots;
    slots.reserve(kCraftSlotCount);
    for (core::u32 slot = 0; slot < kCraftSlotCount; ++slot) {
        CraftSlotInfo info;
        info.slot = slot;
        const std::filesystem::path file = craft_slot_path(dir, slot);
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) {
            slots.push_back(std::move(info));
            continue;
        }
        const core::Result<std::string> json = load_craft_from_path(file);
        if (!json.has_value()) {
            info.unreadable = true;
            slots.push_back(std::move(info));
            continue;
        }
        // Parse defensively: a corrupt craft must not break the listing.
        const core::Result<Craft> craft = craft_from_json(json.value());
        info.occupied = craft.has_value();
        info.unreadable = !craft.has_value();
        if (craft.has_value()) {
            info.name = craft.value().meta.name;
            info.engine = craft.value().meta.engine;
            info.milestone = craft.value().meta.milestone;
        }
        slots.push_back(std::move(info));
    }
    return slots;
}

core::Result<void> save_craft_to_path(const std::filesystem::path& file,
                                      const std::string_view json) noexcept {
    const std::filesystem::path directory = file.has_parent_path() ? file.parent_path() : ".";

    core::Result<std::filesystem::path> dir = ensure_craft_dir(directory);
    if (!dir.has_value()) {
        return core::Result<void>::failure(dir.error().code, dir.error().message);
    }

    // Temp file in the SAME directory — rename(2) stays on one filesystem.
    std::filesystem::path tmp;
    {
        std::error_code ec;
        for (int attempt = 0; attempt < 16; ++attempt) {
            tmp = directory / (".tmp_craft_" + std::to_string(std::rand()) + "_" +
                               std::to_string(attempt) + ".part");
            if (!std::filesystem::exists(tmp, ec)) {
                break;
            }
            tmp.clear();
        }
        if (tmp.empty()) {
            return core::Result<void>::failure(core::ErrorCode::Unknown,
                                               "craft: could not allocate a temp file name");
        }
    }

    {
        std::ofstream stream(tmp, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return core::Result<void>::failure(core::ErrorCode::Unknown,
                                               "craft: cannot open temp file for writing");
        }
        stream << json;
        stream.flush();
        if (!stream) {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return core::Result<void>::failure(core::ErrorCode::Unknown,
                                               "craft: failed while writing temp file");
        }
    }
#if !defined(_WIN32)
    // fsync before rename so the bytes survive a power cut, then fsync the
    // directory so the rename itself is durable.
    const int fd = ::open(tmp.string().c_str(), O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)::fsync(fd);
        (void)::close(fd);
    }
#endif

    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return core::Result<void>::failure(
            core::ErrorCode::Unknown,
            std::string("craft: rename failed: ") + ec.message());
    }
#if !defined(_WIN32)
    const int dir_fd = ::open(directory.string().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir_fd >= 0) {
        (void)::fsync(dir_fd);
        (void)::close(dir_fd);
    }
#endif
    return core::Result<void>::ok();
}

core::Result<std::string> load_craft_from_path(const std::filesystem::path& file) noexcept {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return core::Result<std::string>::failure(
            core::ErrorCode::NotFound, std::string("craft: cannot open '") + file.string() + "'");
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        return core::Result<std::string>::failure(
            core::ErrorCode::Unknown, std::string("craft: failed while reading '") + file.string() + "'");
    }
    std::string out = buffer.str();
    if (out.size() > (1u << 20)) {
        return core::Result<std::string>::failure(core::ErrorCode::InvalidArgument,
                                                  "craft: file exceeds the 1 MiB cap");
    }
    return core::Result<std::string>::ok(std::move(out));
}

core::Result<void> save_craft_to_slot(const std::filesystem::path& dir, const core::u32 slot,
                                      const std::string_view json) noexcept {
    if (slot >= kCraftSlotCount) {
        return core::Result<void>::failure(core::ErrorCode::InvalidArgument,
                                           "craft: slot must be < 9");
    }
    return save_craft_to_path(craft_slot_path(dir, slot), json);
}

core::Result<std::string> load_craft_from_slot(const std::filesystem::path& dir,
                                               const core::u32 slot) noexcept {
    if (slot >= kCraftSlotCount) {
        return core::Result<std::string>::failure(core::ErrorCode::InvalidArgument,
                                                  "craft: slot must be < 9");
    }
    return load_craft_from_path(craft_slot_path(dir, slot));
}

}  // namespace aegis::serial
