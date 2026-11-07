// aegis_craft_check — Milestone 10 tool: craft save/load round-trip proof.
//
// For each built-in preset (SPARROW HOPPER, TWIN-TANK ORBITER, RADIAL TRIPLE):
//   1. serialize the assembly + staging to JSON,
//   2. load the JSON back into a fresh assembly/staging,
//   3. compare parts (template, cells, rotation/flip, tank fill), staging
//      assignments, mass, CoM, Izz — bitwise where the types allow,
//   4. re-serialize and byte-compare the two documents (determinism),
// plus a corrupt-input corpus (each case must fail cleanly, never crash).
//
// Exit code 0 on success (or when the game presets cannot be loaded), 1 on a
// real mismatch. Headless; registered with ctest.
#include <aegis/core/log.hpp>
#include <aegis/core/version.hpp>
#include <aegis/parts/part_template.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>
#include <aegis/serial/craft.hpp>
#include <aegis/serial/craft_store.hpp>

#include "game_presets.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using aegis::core::u32;

constexpr std::size_t kPresetCount = aegis::game::kPresetCount;

bool compare_assemblies(const aegis::parts::VehicleAssembly& a, const aegis::parts::VehicleAssembly& b,
                        const char* label) noexcept {
    bool ok = true;
    const auto& parts_a = a.parts();
    const auto& parts_b = b.parts();
    if (parts_a.size() != parts_b.size()) {
        AEGIS_LOG_ERROR("[craft_check] " << label << ": part count mismatch (" << parts_a.size()
                                          << " vs " << parts_b.size() << ")");
        return false;
    }
    u32 alive = 0;
    for (std::size_t i = 0; i < parts_a.size(); ++i) {
        const auto& pa = parts_a[i];
        const auto& pb = parts_b[i];
        if (pa.tmpl == aegis::parts::kInvalidTemplate || pb.tmpl == aegis::parts::kInvalidTemplate) {
            if (pa.tmpl != pb.tmpl) {
                AEGIS_LOG_ERROR("[craft_check] " << label << ": tombstone mismatch at index " << i);
                ok = false;
            }
            continue;
        }
        ++alive;
        bool part_ok = pa.tmpl == pb.tmpl && pa.gx == pb.gx && pa.gy == pb.gy && pa.rot == pb.rot &&
                       pa.fuel_type == pb.fuel_type &&
                       std::memcmp(&pa.propellant, &pb.propellant, sizeof(pa.propellant)) == 0;
        if (!part_ok) {
            AEGIS_LOG_ERROR("[craft_check] " << label << ": part " << i << " differs");
            ok = false;
        }
    }
    if (a.alive_count() != b.alive_count()) {
        AEGIS_LOG_ERROR("[craft_check] " << label << ": alive count mismatch");
        ok = false;
    }
    const aegis::parts::MassProperties& ma = a.mass_properties();
    const aegis::parts::MassProperties& mb = b.mass_properties();
    auto same_f32 = [](const float x, const float y) {
        return std::memcmp(&x, &y, sizeof(x)) == 0;
    };
    if (!same_f32(ma.dry_mass, mb.dry_mass) || !same_f32(ma.propellant_mass, mb.propellant_mass) ||
        !same_f32(ma.total_mass, mb.total_mass) ||
        !same_f32(ma.center_of_mass[0], mb.center_of_mass[0]) ||
        !same_f32(ma.center_of_mass[1], mb.center_of_mass[1]) || !same_f32(ma.inertia_izz, mb.inertia_izz)) {
        AEGIS_LOG_ERROR("[craft_check] " << label << ": mass properties differ");
        ok = false;
    }
    return ok;
}

}  // namespace

int main() {
    AEGIS_LOG_INFO("Aegis2D — Milestone 10: aegis_craft_check (v" << aegis::core::kVersionString << ")");
    int failures = 0;

    for (std::size_t preset = 0; preset < kPresetCount; ++preset) {
        const char* name = aegis::game::preset_info(static_cast<u32>(preset)).name;
        aegis::parts::VehicleAssembly assembly = aegis::game::make_preset_rocket(static_cast<u32>(preset));
        aegis::parts::StagingList staging;
        (void)staging.auto_assign(assembly);

        aegis::serial::CraftMeta meta;
        meta.name = name;
        meta.engine = aegis::core::kVersionString;
        meta.milestone = static_cast<u32>(aegis::core::kMilestone);

        const std::string json = aegis::serial::craft_to_json(assembly, staging, meta);

        auto loaded = aegis::serial::craft_from_json(json);
        if (!loaded) {
            AEGIS_LOG_ERROR("[craft_check] " << name << ": load failed: " << loaded.error().to_string());
            ++failures;
            continue;
        }

        const bool assembly_ok = compare_assemblies(assembly, loaded.value().assembly, name);

        // Staging: every alive staged part must land on the same stage number.
        bool staging_ok = true;
        for (std::size_t i = 0; i < assembly.parts().size(); ++i) {
            const auto part = static_cast<aegis::parts::PartIndex>(i);
            if (assembly.parts()[i].tmpl == aegis::parts::kInvalidTemplate) {
                continue;
            }
            if (staging.is_staged(part) != loaded.value().staging.is_staged(part) ||
                (staging.is_staged(part) && staging.stage_of(part) != loaded.value().staging.stage_of(part))) {
                AEGIS_LOG_ERROR("[craft_check] " << name << ": staging differs at part " << i);
                staging_ok = false;
            }
        }
        if (!staging_ok) {
            ++failures;
            continue;
        }
        if (!assembly_ok) {
            ++failures;
            continue;
        }

        // Determinism: re-serialize and byte-compare.
        const std::string json2 = aegis::serial::craft_to_json(loaded.value().assembly,
                                                               loaded.value().staging,
                                                               loaded.value().meta);
        if (json != json2) {
            AEGIS_LOG_ERROR("[craft_check] " << name << ": re-serialization differs");
            ++failures;
            continue;
        }
        AEGIS_LOG_INFO("[craft_check] " << name << ": round-trip OK (" << assembly.alive_count()
                                        << " parts, " << json.size() << " bytes)");
    }

    // Corrupt-input corpus: every case must be rejected without crashing.
    const std::vector<std::pair<const char*, const char*>> corpus = {
        {"truncated", "{\"kind\": \"aegis-craft\""},
        {"wrong kind", "{\"kind\": \"other\", \"version\": 1}"},
        {"version 2", "{\"kind\": \"aegis-craft\", \"version\": 2}"},
        {"duplicate key", "{\"kind\": \"aegis-craft\", \"kind\": \"aegis-craft\"}"},
        {"unknown template",
         "{\"kind\": \"aegis-craft\", \"version\": 1, \"parts\":"
         " [{\"template\": \"not_a_template\", \"gx\": 0, \"gy\": 0, \"rot\": 0, \"flip\": false}]}"},
        {"overlapping parts",
         "{\"kind\": \"aegis-craft\", \"version\": 1, \"parts\":"
         " [{\"template\": \"mk1_pod\", \"gx\": 0, \"gy\": 0, \"rot\": 0, \"flip\": false},"
         "  {\"template\": \"short_1_tank\", \"gx\": 0, \"gy\": 0, \"rot\": 0, \"flip\": false}]}"},
        {"bad stage index",
         "{\"kind\": \"aegis-craft\", \"version\": 1, \"parts\":"
         " [{\"template\": \"sparrow_engine\", \"gx\": 0, \"gy\": 0, \"rot\": 0, \"flip\": false}],"
         "  \"staging\": [{\"part\": 0, \"stage\": 99}]}"},
        {"stage target out of bounds",
         "{\"kind\": \"aegis-craft\", \"version\": 1, \"parts\": [],"
         "  \"staging\": [{\"part\": 12, \"stage\": 0}]}"},
    };
    for (const auto& [label, document] : corpus) {
        if (aegis::serial::craft_from_json(document).has_value()) {
            AEGIS_LOG_ERROR("[craft_check] corrupt corpus case '" << label << "' was ACCEPTED");
            ++failures;
        }
    }
    AEGIS_LOG_INFO("[craft_check] corrupt-input corpus rejected as expected");

    // Slot store: save every preset into a fresh overridden slot directory,
    // list the slots, load them back and re-verify determinism.
    const std::filesystem::path slot_dir =
        std::filesystem::temp_directory_path() / "aegis_craft_check_slots";
    {
        std::error_code ec;
        std::filesystem::remove_all(slot_dir, ec);
    }
#if defined(_WIN32)
    _putenv_s("AEGIS_CRAFT_DIR", slot_dir.string().c_str());
#else
    setenv("AEGIS_CRAFT_DIR", slot_dir.string().c_str(), 1);
#endif
    const std::filesystem::path override_dir = aegis::serial::default_craft_dir();
    if (override_dir != slot_dir) {
        AEGIS_LOG_ERROR("[craft_check] AEGIS_CRAFT_DIR override not honored");
        ++failures;
    }
    for (std::size_t preset = 0; preset < kPresetCount; ++preset) {
        aegis::parts::VehicleAssembly assembly = aegis::game::make_preset_rocket(static_cast<u32>(preset));
        aegis::parts::StagingList staging;
        (void)staging.auto_assign(assembly);
        aegis::serial::CraftMeta meta;
        meta.name = aegis::game::preset_info(static_cast<u32>(preset)).name;
        meta.engine = aegis::core::kVersionString;
        meta.milestone = static_cast<u32>(aegis::core::kMilestone);
        const std::string json = aegis::serial::craft_to_json(assembly, staging, meta);
        const auto slot = static_cast<u32>(preset);
        if (aegis::serial::save_craft_to_slot(slot_dir, slot, json).has_value() == false) {
            AEGIS_LOG_ERROR("[craft_check] slot save failed for preset " << preset);
            ++failures;
            continue;
        }
    }
    const std::vector<aegis::serial::CraftSlotInfo> slots = aegis::serial::list_craft_slots(slot_dir);
    if (slots.size() != aegis::serial::kCraftSlotCount) {
        AEGIS_LOG_ERROR("[craft_check] slot listing returned " << slots.size() << " entries");
        ++failures;
    }
    for (std::size_t preset = 0; preset < kPresetCount; ++preset) {
        const u32 slot = static_cast<u32>(preset);
        const aegis::serial::CraftSlotInfo& info = slots[slot];
        if (!info.occupied) {
            AEGIS_LOG_ERROR("[craft_check] slot " << slot << " should be occupied");
            ++failures;
            continue;
        }
        auto loaded_json = aegis::serial::load_craft_from_slot(slot_dir, slot);
        if (!loaded_json) {
            AEGIS_LOG_ERROR("[craft_check] slot " << slot << " load failed");
            ++failures;
            continue;
        }
        auto loaded = aegis::serial::craft_from_json(loaded_json.value());
        if (!loaded) {
            AEGIS_LOG_ERROR("[craft_check] slot " << slot << " craft parse failed");
            ++failures;
            continue;
        }
        aegis::parts::VehicleAssembly assembly = aegis::game::make_preset_rocket(static_cast<u32>(preset));
        if (!compare_assemblies(assembly, loaded.value().assembly, info.name.c_str())) {
            ++failures;
            continue;
        }
        AEGIS_LOG_INFO("[craft_check] slot " << slot << " holds '" << info.name << "' (verified)");
    }
    // An empty slot must report unoccupied; a corrupt slot file must not crash listing.
    if (slots.size() == aegis::serial::kCraftSlotCount) {
        for (u32 i = static_cast<u32>(kPresetCount); i < aegis::serial::kCraftSlotCount; ++i) {
            if (slots[i].occupied || slots[i].unreadable) {
                AEGIS_LOG_ERROR("[craft_check] slot " << i << " expected empty");
                ++failures;
            }
        }
    }
    {
        std::error_code ec;
        std::ofstream junk(slot_dir / "craft_7.aegiscraft");
        junk << "definitely not json {{{";
        junk.close();
        const auto slots7 = aegis::serial::list_craft_slots(slot_dir);
        if (!slots7[7].unreadable || slots7[7].occupied) {
            AEGIS_LOG_ERROR("[craft_check] corrupt slot 7 not reported as unreadable");
            ++failures;
        }
    }
    {
        std::error_code ec;
        std::filesystem::remove_all(slot_dir, ec);
    }
    AEGIS_LOG_INFO("[craft_check] slot store verified");

    if (failures != 0) {
        AEGIS_LOG_ERROR("[craft_check] " << failures << " preset(s) failed");
        return 1;
    }
    AEGIS_LOG_INFO("[craft_check] SERIAL/CHECK_VERIFIED");
    return 0;
}
