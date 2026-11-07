// Aegis2D — craft slot persistence (Milestone 10): files on disk, atomically.
//
// Save location (decision 2): the platform data directory, overridable with
// the AEGIS_CRAFT_DIR environment variable (used by tools and tests):
//   Linux/BSD : $XDG_DATA_HOME/aegis2d/craft  (default ~/.local/share/aegis2d/craft)
//   macOS     : ~/Library/Application Support/aegis2d/craft
//   Windows   : %APPDATA%/aegis2d/craft
//
// Slot files are named "craft_<n>.aegiscraft" with n in [0, kCraftSlotCount).
// Explicit paths are also supported for tools/tests. All writes go through a
// temporary file in the same directory followed by a rename, so a crash
// mid-save can never leave a half-written craft behind.
#pragma once

#include <aegis/export.h>

#include <aegis/core/result.hpp>
#include <aegis/core/types.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace aegis::serial {

inline constexpr core::u32 kCraftSlotCount = 9;

/// Directory used by default. Reads AEGIS_CRAFT_DIR first, then platform
/// conventions. The directory is NOT created here — call ensure_craft_dir().
[[nodiscard]] AEGIS_API std::filesystem::path default_craft_dir() noexcept;

/// Creates the directory (and parents) when missing. Returns the path.
[[nodiscard]] AEGIS_API core::Result<std::filesystem::path> ensure_craft_dir(
    const std::filesystem::path& dir) noexcept;

[[nodiscard]] AEGIS_API std::filesystem::path craft_slot_path(const std::filesystem::path& dir,
                                                    core::u32 slot) noexcept;

struct AEGIS_API CraftSlotInfo {
    core::u32 slot = 0;
    bool occupied = false;           ///< file exists and parses as a craft
    bool unreadable = false;         ///< file exists but failed to parse
    std::string name;                ///< meta.name (empty when unreadable)
    std::string engine;              ///< meta.engine
    core::u32 milestone = 0;         ///< meta.milestone
};

/// Lists kCraftSlotCount slots in order. A missing directory yields all-empty
/// slots; an unreadable file is reported as unreadable, never fatal.
[[nodiscard]] AEGIS_API std::vector<CraftSlotInfo> list_craft_slots(
    const std::filesystem::path& dir) noexcept;

/// Atomically writes `json` to `file` (temp file + rename, same directory).
[[nodiscard]] AEGIS_API core::Result<void> save_craft_to_path(const std::filesystem::path& file,
                                                    std::string_view json) noexcept;

/// Reads a whole craft file. Error with InvalidArgument on failure.
[[nodiscard]] AEGIS_API core::Result<std::string> load_craft_from_path(
    const std::filesystem::path& file) noexcept;

/// Saves into a slot. Creates the directory when missing. `slot` must be
/// < kCraftSlotCount.
[[nodiscard]] AEGIS_API core::Result<void> save_craft_to_slot(const std::filesystem::path& dir,
                                                    core::u32 slot,
                                                    std::string_view json) noexcept;

/// Loads a slot's JSON. Slot must be < kCraftSlotCount.
[[nodiscard]] AEGIS_API core::Result<std::string> load_craft_from_slot(const std::filesystem::path& dir,
                                                             core::u32 slot) noexcept;

}  // namespace aegis::serial
