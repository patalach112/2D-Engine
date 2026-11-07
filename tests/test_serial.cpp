// Aegis2D — M10 serial module tests: strict JSON parser/writer + craft
// round-trip, corrupt-input rejection, tombstone remapping, game presets.
#include <aegis/parts/part_template.hpp>
#include <aegis/parts/staging.hpp>
#include <aegis/parts/vehicle_assembly.hpp>
#include <aegis/serial/craft.hpp>
#include <aegis/serial/json.hpp>
#include <aegis/core/version.hpp>

#include <aegis/serial/craft_store.hpp>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aegis_test.hpp"
#include "game_presets.hpp"

namespace aegis::serial {
namespace {

using aegis::parts::PartIndex;
using aegis::parts::TemplateId;

bool assemblies_equal(const aegis::parts::VehicleAssembly& a, const aegis::parts::VehicleAssembly& b) {
    if (a.alive_count() != b.alive_count()) {
        return false;
    }
    // Compare ALIVE parts in order — a save never contains tombstones.
    std::vector<std::size_t> alive_a;
    for (std::size_t i = 0; i < a.parts().size(); ++i) {
        if (a.parts()[i].tmpl != aegis::parts::kInvalidTemplate) {
            alive_a.push_back(i);
        }
    }
    std::vector<std::size_t> alive_b;
    for (std::size_t i = 0; i < b.parts().size(); ++i) {
        if (b.parts()[i].tmpl != aegis::parts::kInvalidTemplate) {
            alive_b.push_back(i);
        }
    }
    if (alive_a.size() != alive_b.size()) {
        return false;
    }
    for (std::size_t k = 0; k < alive_a.size(); ++k) {
        const aegis::parts::Part& pa = a.parts()[alive_a[k]];
        const aegis::parts::Part& pb = b.parts()[alive_b[k]];
        if (pa.tmpl != pb.tmpl || pa.gx != pb.gx || pa.gy != pb.gy || pa.rot != pb.rot) {
            return false;
        }
        if (pa.propellant != pb.propellant) {
            return false;  // exact f32 compare — serialize/parse preserves bits
        }
    }
    const aegis::parts::MassProperties& ma = a.mass_properties();
    const aegis::parts::MassProperties& mb = b.mass_properties();
    return ma.dry_mass == mb.dry_mass && ma.propellant_mass == mb.propellant_mass &&
           ma.total_mass == mb.total_mass && ma.inertia_izz == mb.inertia_izz &&
           ma.center_of_mass[0] == mb.center_of_mass[0] && ma.center_of_mass[1] == mb.center_of_mass[1];
}

bool staging_equal(const aegis::parts::StagingList& a, const aegis::parts::StagingList& b,
                   const aegis::parts::VehicleAssembly& assembly) {
    for (std::size_t i = 0; i < assembly.parts().size(); ++i) {
        if (assembly.parts()[i].tmpl == aegis::parts::kInvalidTemplate) {
            continue;
        }
        const PartIndex idx = static_cast<PartIndex>(i);
        if (a.is_staged(idx) != b.is_staged(idx)) {
            return false;
        }
        if (a.is_staged(idx) && a.stage_of(idx) != b.stage_of(idx)) {
            return false;
        }
    }
    return true;
}

}  // namespace

AEGIS_TEST(serial_json, "parses primitives and structure") {
    const aegis::core::Result<Json> parsed =
        parse_json(R"({"s": "hi", "n": 1400, "neg": -2.5, "b": true, "z": false, "null": null,
                       "arr": [1, 2, {"deep": [true, null]}]})");
    AEGIS_REQUIRE(parsed.has_value());
    const Json& root = parsed.value();
    AEGIS_REQUIRE(root.is_object());
    const Json* s = root.find("s");
    AEGIS_REQUIRE(s != nullptr && s->is_string());
    AEGIS_CHECK_EQ(s->as_string(), std::string("hi"));
    AEGIS_CHECK(root.find("b")->as_bool());
    AEGIS_CHECK(!root.find("z")->as_bool());
    const Json* n = root.find("n");
    AEGIS_REQUIRE(n != nullptr && n->is_integral());
    AEGIS_CHECK_EQ(n->as_integral(), 1400);
    const Json* neg = root.find("neg");
    AEGIS_REQUIRE(neg != nullptr);
    AEGIS_CHECK(std::fabs(neg->as_number() + 2.5) < 1e-12);
    const Json* arr = root.find("arr");
    AEGIS_REQUIRE(arr != nullptr && arr->is_array());
    AEGIS_CHECK_EQ(arr->as_array().size(), std::size_t{3});
    const Json* deep = arr->as_array()[2].find("deep");
    AEGIS_REQUIRE(deep != nullptr && deep->is_array());
    AEGIS_CHECK(deep->as_array()[0].is_bool());
    AEGIS_CHECK(root.find("null")->is_null());
}

AEGIS_TEST(serial_json, "rejects garbage and strict violations") {
    const char* cases[] = {
        "{",                      // truncated
        "[1,2,]",                 // trailing comma
        "{\"a\": 01}",            // leading zero
        "{\"a\": NaN}",           // NaN literal
        "{\"a\": Infinity}",      // Infinity literal
        "{\"a\": .5}",            // leading-dot number
        "{\"a\": 1.}",            // trailing-dot number
        "{\"a\": 1e}",            // missing exponent digits
        "{'a': 1}",               // single quotes
        "{a: 1}",                 // unquoted key
        "\"raw \x01 control\"",   // raw control character in string
        "{\"a\" 5}",              // missing colon
        "tru",                    // bad literal
        "\"bad \\q escape\"",     // invalid escape
        "[1 2]",                  // missing comma
        "",                       // empty document
        "   ",                    // whitespace only
        "{\"a\": 1} extra",       // trailing content
    };
    for (const char* document : cases) {
        AEGIS_CHECK(!parse_json(document).has_value());
    }
}

AEGIS_TEST(serial_json, "duplicate object keys are rejected") {
    AEGIS_CHECK(!parse_json("{\"a\": 1, \"a\": 2}").has_value());
    AEGIS_CHECK(!parse_json("{\"outer\": {\"k\": 1, \"k\": 2}}").has_value());
}

AEGIS_TEST(serial_json, "nesting depth cap is enforced") {
    std::string deep;
    for (int i = 0; i < 80; ++i) deep += "[";
    deep += "1";
    for (int i = 0; i < 80; ++i) deep += "]";
    AEGIS_CHECK(!parse_json(deep).has_value());

    std::string shallow;
    for (int i = 0; i < 20; ++i) shallow += "[";
    shallow += "1";
    for (int i = 0; i < 20; ++i) shallow += "]";
    AEGIS_CHECK(parse_json(shallow).has_value());
}

AEGIS_TEST(serial_json, "escapes and unicode round-trip") {
    const aegis::core::Result<Json> parsed = parse_json(
        R"({"esc": "a\b\f\n\r\t\"\\/", "hex": "\u0041", "emoji": "\uD83D\uDE00"})");
    AEGIS_REQUIRE(parsed.has_value());
    const Json* esc = parsed.value().find("esc");
    AEGIS_REQUIRE(esc != nullptr);
    AEGIS_CHECK_EQ(esc->as_string(), std::string("a\b\f\n\r\t\"\\/"));
    const Json* emoji = parsed.value().find("emoji");
    AEGIS_REQUIRE(emoji != nullptr);
    AEGIS_CHECK_EQ(emoji->as_string(), std::string("\xF0\x9F\x98\x80"));  // U+1F600

    AEGIS_CHECK(!parse_json("{\"u\": \"\\uD800\"}").has_value());   // lone high surrogate
    AEGIS_CHECK(!parse_json("{\"u\": \"\\uDCFF\"}").has_value());   // lone low surrogate
    AEGIS_CHECK(!parse_json("{\"u\": \"\\u12zz\"}").has_value());   // bad hex digits
}

AEGIS_TEST(serial_json, "integer range and integral detection") {
    const aegis::core::Result<Json> parsed =
        parse_json(R"({"max_safe": 9007199254740991, "unsafe_big": 9007199254740993,
                        "frac": 1400.25, "neg": -0.5})");
    AEGIS_REQUIRE(parsed.has_value());
    const Json* max_safe = parsed.value().find("max_safe");
    AEGIS_REQUIRE(max_safe != nullptr && max_safe->is_integral());
    AEGIS_CHECK_EQ(max_safe->as_integral(), 9007199254740991LL);
    AEGIS_CHECK(!parsed.value().find("frac")->is_integral());
    AEGIS_CHECK(!parsed.value().find("neg")->is_integral());
}

AEGIS_TEST(serial_json, "writer then parser preserves values") {
    const aegis::core::Result<Json> parsed =
        parse_json(R"({"small": 5, "big": 9007199254740991, "frac": 1400.25, "s": "x\ny"})");
    AEGIS_REQUIRE(parsed.has_value());
    const std::string written = to_json(parsed.value());
    const aegis::core::Result<Json> reparsed = parse_json(written);
    AEGIS_REQUIRE(reparsed.has_value());
    const Json* big = reparsed.value().find("big");
    AEGIS_REQUIRE(big != nullptr && big->is_integral());
    AEGIS_CHECK_EQ(big->as_integral(), 9007199254740991LL);
    const Json* frac = reparsed.value().find("frac");
    AEGIS_REQUIRE(frac != nullptr);
    AEGIS_CHECK(std::fabs(frac->as_number() - 1400.25) < 1e-12);
    const Json* s = reparsed.value().find("s");
    AEGIS_REQUIRE(s != nullptr);
    AEGIS_CHECK_EQ(s->as_string(), std::string("x\ny"));
}

AEGIS_TEST(serial_craft, "hand-built stack round-trips exactly") {
    aegis::parts::VehicleAssembly assembly;
    const aegis::core::Result<PartIndex> sparrow =
        assembly.add_root(4, 0, 0, 0);  // Sparrow engine at the origin
    AEGIS_REQUIRE(sparrow.has_value());
    const aegis::core::Result<PartIndex> decoupler =
        assembly.snap(sparrow.value(), 0, 7, 1, 0);
    AEGIS_REQUIRE(decoupler.has_value());
    const aegis::core::Result<PartIndex> tank =
        assembly.snap(decoupler.value(), 0, 3, 1, 0);  // short tank
    AEGIS_REQUIRE(tank.has_value());
    const aegis::core::Result<PartIndex> pod =
        assembly.snap(tank.value(), 0, 0, 0, 0);  // pod on top
    AEGIS_REQUIRE(pod.has_value());

    aegis::parts::StagingList staging;
    (void)staging.auto_assign(assembly);

    CraftMeta meta;
    meta.name = "TEST-CRAFT";
    meta.engine = aegis::core::kVersionString;
    meta.milestone = static_cast<aegis::core::u32>(aegis::core::kMilestone);

    const std::string first = craft_to_json(assembly, staging, meta);
    const aegis::core::Result<Craft> loaded = craft_from_json(first);
    AEGIS_REQUIRE(loaded.has_value());
    if (!loaded.has_value()) {
        return;
    }
    AEGIS_CHECK(assemblies_equal(assembly, loaded.value().assembly));
    AEGIS_CHECK(staging_equal(staging, loaded.value().staging, assembly));

    const std::string second =
        craft_to_json(loaded.value().assembly, loaded.value().staging, loaded.value().meta);
    AEGIS_CHECK_EQ(first, second);
}

AEGIS_TEST(serial_craft, "tombstoned parts are skipped and staging remaps") {
    aegis::parts::VehicleAssembly assembly;
    AEGIS_REQUIRE(assembly.add_root(4, 0, 0, 0).has_value());  // Sparrow (no pod below the cut)
    const aegis::core::Result<PartIndex> tank =
        assembly.snap(0, 0, 3, 1, 0);  // short tank above the Sparrow
    AEGIS_REQUIRE(tank.has_value());
    AEGIS_REQUIRE(assembly.snap(tank.value(), 0, 0, 0, 0).has_value());  // pod on top
    AEGIS_REQUIRE(assembly.remove(tank.value()).has_value());  // tank + Sparrow die; pod keeps a pod → stays

    aegis::parts::StagingList staging;
    (void)staging.auto_assign(assembly);

    CraftMeta meta;
    const std::string json = craft_to_json(assembly, staging, meta);
    const aegis::core::Result<Craft> loaded = craft_from_json(json);
    AEGIS_REQUIRE(loaded.has_value());
    if (!loaded.has_value()) {
        return;
    }
    AEGIS_CHECK_EQ(loaded.value().assembly.alive_count(), 1u);
    AEGIS_CHECK_EQ(loaded.value().assembly.parts().size(), std::size_t{1});
    AEGIS_CHECK_EQ(loaded.value().assembly.parts()[0].tmpl, 0);  // the pod survives
    AEGIS_CHECK(assemblies_equal(assembly, loaded.value().assembly));
}

AEGIS_TEST(serial_craft, "game presets round-trip bit exactly") {
    for (aegis::core::u32 preset = 0; preset < aegis::game::kPresetCount; ++preset) {
        aegis::parts::VehicleAssembly assembly = aegis::game::make_preset_rocket(preset);
        aegis::parts::StagingList staging;
        (void)staging.auto_assign(assembly);
        CraftMeta meta;
        meta.name = aegis::game::preset_info(preset).name;
        meta.engine = aegis::core::kVersionString;
        meta.milestone = static_cast<aegis::core::u32>(aegis::core::kMilestone);

        const std::string first = craft_to_json(assembly, staging, meta);
        const aegis::core::Result<Craft> loaded = craft_from_json(first);
        AEGIS_REQUIRE(loaded.has_value());
        if (!loaded.has_value()) {
            return;
        }
        AEGIS_CHECK(assemblies_equal(assembly, loaded.value().assembly));
        AEGIS_CHECK(staging_equal(staging, loaded.value().staging, assembly));
        const std::string second =
            craft_to_json(loaded.value().assembly, loaded.value().staging, loaded.value().meta);
        AEGIS_CHECK_EQ(first, second);
    }
}

AEGIS_TEST(serial_craft, "rejects unknown template unknown stage and bad part ref") {
    const char* unknown_tmpl =
        R"({"kind": "aegis-craft", "version": 1, "parts":
            [{"template": "nope_01", "gx": 0, "gy": 0, "rot": 0, "flip": false}]})";
    AEGIS_CHECK(!craft_from_json(unknown_tmpl).has_value());

    const char* bad_stage =
        R"({"kind": "aegis-craft", "version": 1, "parts":
            [{"template": "sparrow_engine", "gx": 0, "gy": 0, "rot": 0, "flip": false}],
            "staging": [{"part": 0, "stage": 42}]})";
    AEGIS_CHECK(!craft_from_json(bad_stage).has_value());

    const char* bad_part_ref =
        R"({"kind": "aegis-craft", "version": 1, "parts": [],
            "staging": [{"part": 0, "stage": 0}]})";
    AEGIS_CHECK(!craft_from_json(bad_part_ref).has_value());

    const char* bad_version =
        R"({"kind": "aegis-craft", "version": 2, "parts": []})";
    AEGIS_CHECK(!craft_from_json(bad_version).has_value());

    const char* bad_kind =
        R"({"kind": "aegis-other", "version": 1, "parts": []})";
    AEGIS_CHECK(!craft_from_json(bad_kind).has_value());
}

AEGIS_TEST(serial_craft, "propellant is clamped to tank capacity") {
    const char* overfull =
        R"({"kind": "aegis-craft", "version": 1, "parts":
            [{"template": "short_1_tank", "gx": 0, "gy": 0, "rot": 0, "flip": false,
               "propellant": 99999999.0}]})";
    const aegis::core::Result<Craft> loaded = craft_from_json(overfull);
    AEGIS_REQUIRE(loaded.has_value());
    if (!loaded.has_value() || loaded.value().assembly.parts().empty()) {
        return;
    }
    const aegis::parts::PartTemplate* tank = aegis::parts::find_template_by_key("short_1_tank");
    AEGIS_REQUIRE(tank != nullptr);
    AEGIS_CHECK_EQ(loaded.value().assembly.parts()[0].propellant, tank->tank.propellant);
}

AEGIS_TEST(serial_store, "slot save and load round-trip through real files") {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "aegis_test_serial_store";
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    aegis::parts::VehicleAssembly assembly = aegis::game::make_preset_rocket(0);
    aegis::parts::StagingList staging;
    (void)staging.auto_assign(assembly);
    CraftMeta meta;
    meta.name = "SLOT-TEST";
    meta.engine = aegis::core::kVersionString;
    const std::string json = craft_to_json(assembly, staging, meta);

    AEGIS_REQUIRE(aegis::serial::save_craft_to_slot(dir, 3, json).has_value());
    const aegis::core::Result<std::string> loaded_json =
        aegis::serial::load_craft_from_slot(dir, 3);
    AEGIS_REQUIRE(loaded_json.has_value());
    AEGIS_CHECK_EQ(loaded_json.value(), json);

    const std::vector<aegis::serial::CraftSlotInfo> slots = aegis::serial::list_craft_slots(dir);
    AEGIS_CHECK_EQ(slots.size(), std::size_t{aegis::serial::kCraftSlotCount});
    AEGIS_CHECK(slots[3].occupied);
    AEGIS_CHECK_EQ(slots[3].name, std::string("SLOT-TEST"));
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i != 3) {
            AEGIS_CHECK(!slots[i].occupied && !slots[i].unreadable);
        }
    }

    AEGIS_CHECK(aegis::serial::save_craft_to_slot(dir, 9, json).has_value() == false);  // out of range
    AEGIS_CHECK(aegis::serial::save_craft_to_path(dir / "custom.aegiscraft", json).has_value());
    AEGIS_CHECK(std::filesystem::exists(dir / "custom.aegiscraft"));

    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
}

}  // namespace aegis::serial
