// Aegis2D — craft save/load, implementation (Milestone 10).
#include <aegis/serial/craft.hpp>

#include <aegis/core/version.hpp>
#include <aegis/parts/part_template.hpp>
#include <aegis/serial/json.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace aegis::serial {

namespace {

constexpr core::i32 kMaxGridCoord = 32767;
constexpr core::i32 kMinGridCoord = -32768;

[[nodiscard]] core::Result<std::string> require_string(const Json& parent, const char* key) {
    const Json* value = parent.find(key);
    if (value == nullptr || !value->is_string()) {
        return core::Result<std::string>::failure(core::ErrorCode::InvalidArgument,
                                                  std::string("missing or non-string '") + key + "'");
    }
    return core::Result<std::string>::ok(value->as_string());
}

[[nodiscard]] core::Result<core::i64> require_integral(const Json& parent, const char* key,
                                                       const core::i64 min, const core::i64 max) {
    const Json* value = parent.find(key);
    if (value == nullptr || !value->is_integral()) {
        return core::Result<core::i64>::failure(core::ErrorCode::InvalidArgument,
                                                std::string("missing, non-integral or out-of-range '") + key + "'");
    }
    const core::i64 v = value->as_integral();
    if (v < min || v > max) {
        return core::Result<core::i64>::failure(core::ErrorCode::InvalidArgument,
                                                std::string("'") + key + "' is out of range");
    }
    return core::Result<core::i64>::ok(v);
}

[[nodiscard]] core::Result<bool> require_bool(const Json& parent, const char* key) {
    const Json* value = parent.find(key);
    if (value == nullptr || !value->is_bool()) {
        return core::Result<bool>::failure(core::ErrorCode::InvalidArgument,
                                           std::string("missing or non-boolean '") + key + "'");
    }
    return core::Result<bool>::ok(value->as_bool());
}

[[nodiscard]] core::Result<double> optional_number(const Json& parent, const char* key,
                                                   const double fallback) {
    const Json* value = parent.find(key);
    if (value == nullptr) {
        return core::Result<double>::ok(fallback);
    }
    if (!value->is_number() || !std::isfinite(value->as_number())) {
        return core::Result<double>::failure(core::ErrorCode::InvalidArgument,
                                             std::string("'") + key + "' must be a finite number");
    }
    return core::Result<double>::ok(value->as_number());
}

}  // namespace

std::string craft_to_json(const parts::VehicleAssembly& assembly, const parts::StagingList& staging,
                          const CraftMeta& meta) {
    // Assembly indices survive as file indices (tombstoned parts are skipped,
    // but the file's parts array order matches insertion order, and the fresh
    // loader assembly is rebuilt in the same order, keeping indices stable).
    std::vector<core::u32> file_index_of(assembly.parts().size(), ~core::u32{0});
    Json j_parts = Json::make_array();
    for (std::size_t i = 0; i < assembly.parts().size(); ++i) {
        const parts::Part& part = assembly.parts()[i];
        if (part.tmpl == parts::kInvalidTemplate) {
            continue;  // tombstone — skip
        }
        const parts::PartTemplate* tmpl = parts::find_template(part.tmpl);
        Json jp = Json::make_object();
        jp.set("template", Json(std::string(tmpl ? tmpl->key : "")));
        jp.set("gx", Json(static_cast<long long>(part.gx)));
        jp.set("gy", Json(static_cast<long long>(part.gy)));
        jp.set("rot", Json(static_cast<long long>(parts::rot_of(part.rot))));
        jp.set("flip", Json(parts::flipped(part.rot)));
        if (tmpl != nullptr && tmpl->is_tank()) {
            jp.set("propellant", Json(static_cast<double>(part.propellant)));
        }
        file_index_of[i] = static_cast<core::u32>(j_parts.as_array().size());
        j_parts.push_back(std::move(jp));
    }

    // Staging: one {part, stage} record per staged live part.
    Json j_staging = Json::make_array();
    for (std::size_t i = 0; i < assembly.parts().size(); ++i) {
        if (assembly.parts()[i].tmpl == parts::kInvalidTemplate) {
            continue;
        }
        if (!staging.is_staged(static_cast<parts::PartIndex>(i))) {
            continue;
        }
        Json item = Json::make_object();
        item.set("part", Json(static_cast<long long>(file_index_of[i])));
        item.set("stage", Json(static_cast<long long>(staging.stage_of(static_cast<parts::PartIndex>(i)))));
        j_staging.push_back(std::move(item));
    }

    Json j_meta = Json::make_object();
    j_meta.set("name", Json(meta.name));
    j_meta.set("engine", Json(meta.engine));
    j_meta.set("milestone", Json(static_cast<long long>(meta.milestone)));

    Json root = Json::make_object();
    root.set("kind", Json(kCraftKind));
    root.set("version", Json(static_cast<long long>(kCraftFormatVersion)));
    root.set("meta", std::move(j_meta));
    root.set("parts", std::move(j_parts));
    root.set("staging", std::move(j_staging));
    return to_json(root);
}

[[nodiscard]] core::Result<Craft> craft_from_json(const std::string_view text) {
    auto parsed = parse_json(text);
    if (!parsed) {
        return core::Result<Craft>::failure(parsed.error().code, parsed.error().message);
    }
    const Json& root = parsed.value();
    if (!root.is_object()) {
        return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument, "craft: root must be an object");
    }

    {
        const Json* kind = root.find("kind");
        if (kind == nullptr || !kind->is_string() || kind->as_string() != kCraftKind) {
            return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                                "craft: missing or wrong 'kind' (expected \"aegis-craft\")");
        }
    }
    {
        const Json* version = root.find("version");
        if (version == nullptr || !version->is_integral() || version->as_integral() != kCraftFormatVersion) {
            return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                                "craft: unsupported or missing 'version' (this build reads version 1)");
        }
    }

    Craft craft;
    if (const Json* meta = root.find("meta")) {
        if (!meta->is_object()) {
            return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument, "craft: 'meta' must be an object");
        }
        if (const Json* name = meta->find("name")) {
            if (name->is_string()) {
                craft.meta.name = name->as_string();
            }
        }
        if (const Json* engine = meta->find("engine")) {
            if (engine->is_string()) {
                craft.meta.engine = engine->as_string();
            }
        }
        if (const Json* milestone = meta->find("milestone")) {
            if (milestone->is_integral() && milestone->as_integral() >= 0) {
                craft.meta.milestone = static_cast<core::u32>(milestone->as_integral());
            }
        }
    }

    const Json* j_parts = root.find("parts");
    if (j_parts == nullptr || !j_parts->is_array()) {
        return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument, "craft: 'parts' must be an array");
    }
    if (j_parts->as_array().size() > parts::kMaxParts) {
        return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                            "craft: too many parts (kMaxParts exceeded)");
    }

    std::vector<std::pair<parts::PartIndex, double>> clamp_propellant;  // assembly index -> target fill
    for (std::size_t i = 0; i < j_parts->as_array().size(); ++i) {
        const Json& jp = j_parts->as_array()[i];
        if (!jp.is_object()) {
            return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                                "craft: parts[] entries must be objects");
        }
        auto key_result = require_string(jp, "template");
        if (!key_result) {
            return core::Result<Craft>::failure(key_result.error().code, key_result.error().message);
        }
        const parts::PartTemplate* tmpl = parts::find_template_by_key(key_result.value());
        if (tmpl == nullptr) {
            return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                                "craft: unknown template key '" + key_result.value() + "'");
        }
        auto gx_result = require_integral(jp, "gx", kMinGridCoord, kMaxGridCoord);
        auto gy_result = require_integral(jp, "gy", kMinGridCoord, kMaxGridCoord);
        auto rot_result = require_integral(jp, "rot", 0, 15);
        auto flip_result = require_bool(jp, "flip");
        if (!gx_result || !gy_result || !rot_result || !flip_result) {
            const core::Result<core::i64>* bad =
                !gx_result ? &gx_result : (!gy_result ? &gy_result : (!rot_result ? &rot_result : nullptr));
            if (bad != nullptr) {
                return core::Result<Craft>::failure(bad->error().code, bad->error().message);
            }
            return core::Result<Craft>::failure(flip_result.error().code, flip_result.error().message);
        }
        const core::u8 rot = parts::make_rot(static_cast<core::u8>(rot_result.value()), flip_result.value());
        auto placed = craft.assembly.add_root(tmpl->id, static_cast<parts::i16>(gx_result.value()),
                                              static_cast<parts::i16>(gy_result.value()), rot);
        if (!placed) {
            return core::Result<Craft>::failure(placed.error().code,
                                                "craft: cannot place part '" + key_result.value() + "': " +
                                                    placed.error().message);
        }
        if (tmpl->is_tank()) {
            auto propellant = optional_number(jp, "propellant", tmpl->tank.propellant);
            if (!propellant) {
                return core::Result<Craft>::failure(propellant.error().code, propellant.error().message);
            }
            const double clamped = std::clamp(propellant.value(), 0.0, static_cast<double>(tmpl->tank.propellant));
            clamp_propellant.emplace_back(static_cast<parts::PartIndex>(i), clamped);
        }
    }
    // Apply fill levels by draining the freshly-placed tanks (add_root starts
    // every tank full).
    for (const auto& [index, target] : clamp_propellant) {
        const parts::Part* part = craft.assembly.part(index);
        if (part == nullptr) {
            continue;
        }
        const parts::PartTemplate* tmpl = parts::find_template(part->tmpl);
        const double full = tmpl ? static_cast<double>(tmpl->tank.propellant) : 0.0;
        const double drain = full - target;
        if (drain > 0.0) {
            (void)craft.assembly.consume_propellant(index, static_cast<parts::f32>(drain));
        }
    }

    if (const Json* j_staging = root.find("staging")) {
        if (!j_staging->is_array()) {
            return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                                "craft: 'staging' must be an array");
        }
        for (const Json& item : j_staging->as_array()) {
            if (!item.is_object()) {
                return core::Result<Craft>::failure(core::ErrorCode::InvalidArgument,
                                                    "craft: staging[] entries must be objects");
            }
            auto part_result = require_integral(item, "part", 0,
                                                static_cast<core::i64>(craft.assembly.parts().size()) - 1);
            auto stage_result = require_integral(item, "stage", 0, parts::kMaxStages - 1);
            if (!part_result || !stage_result) {
                const core::Result<core::i64>* bad = !part_result ? &part_result : &stage_result;
                return core::Result<Craft>::failure(bad->error().code, bad->error().message);
            }
            auto assigned = craft.staging.assign(craft.assembly,
                                                 static_cast<parts::PartIndex>(part_result.value()),
                                                 static_cast<parts::u8>(stage_result.value()));
            if (!assigned) {
                return core::Result<Craft>::failure(assigned.error().code,
                                                    "craft: staging assignment rejected: " + assigned.error().message);
            }
        }
    }

    return core::Result<Craft>::ok(std::move(craft));
}

}  // namespace aegis::serial
