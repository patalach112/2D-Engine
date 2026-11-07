// Aegis2D — staging implementation (Milestone 7).
#include <aegis/parts/staging.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace aegis::parts {

using core::ErrorCode;

namespace {

/// Firing order priority inside a stage: decouplers first, fairings second,
/// engine ignitions last.
u8 action_priority(const StageAction action) noexcept {
    switch (action) {
        case StageAction::FireDecoupler: return 0;
        case StageAction::JettisonFairing: return 1;
        case StageAction::IgniteEngine: return 2;
    }
    return 255;
}

StageAction action_for(const PartCategory category) noexcept {
    switch (category) {
        case PartCategory::Engine: return StageAction::IgniteEngine;
        case PartCategory::Decoupler: return StageAction::FireDecoupler;
        case PartCategory::Fairing: return StageAction::JettisonFairing;
        default: break;
    }
    return StageAction::IgniteEngine;
}

bool is_stageable(const PartCategory category) noexcept {
    return category == PartCategory::Engine || category == PartCategory::Decoupler ||
           category == PartCategory::Fairing;
}

}  // namespace

void StagingList::rebuild_stages() {
    stages_.clear();

    struct Entry {
        u8 stage;
        u8 priority;
        PartIndex part;
        StageAction action;
    };
    std::vector<Entry> entries;
    entries.reserve(assignments_.size());
    for (const auto& [part, stage] : assignments_) {
        const StageAction action = action_kinds_[part];
        entries.push_back({stage, action_priority(action), part, action});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.stage != b.stage) {
            return a.stage < b.stage;
        }
        if (a.priority != b.priority) {
            return a.priority < b.priority;
        }
        return a.part < b.part;
    });

    // Densify: consecutive entries grouped by stage id; stages_ index is the
    // firing order.
    for (std::size_t i = 0; i < entries.size();) {
        std::size_t j = i;
        while (j < entries.size() && entries[j].stage == entries[i].stage) {
            ++j;
        }
        Stage stage_out;
        for (std::size_t k = i; k < j; ++k) {
            stage_out.items.push_back({entries[k].part, entries[k].action});
        }
        stages_.push_back(std::move(stage_out));
        i = j;
    }
    if (cursor_ > stages_.size()) {
        cursor_ = static_cast<u8>(stages_.size());
    }
}

core::Result<void> StagingList::assign(const VehicleAssembly& asmbl, const PartIndex part,
                                       const u8 stage) {
    const Part* p = asmbl.part(part);
    if (!p) {
        return core::Result<void>::failure(ErrorCode::InvalidArgument,
                                           "assign(): unknown or dead part index");
    }
    const PartTemplate* tmpl = find_template(p->tmpl);
    if (!tmpl || !is_stageable(tmpl->category)) {
        return core::Result<void>::failure(ErrorCode::InvalidArgument,
                                           "assign(): only engines, decouplers and fairings are stageable");
    }
    assignments_[part] = stage;
    action_kinds_[part] = action_for(tmpl->category);
    rebuild_stages();
    return core::Result<void>::ok();
}

core::Result<void> StagingList::unassign(const PartIndex part) {
    assignments_.erase(part);
    action_kinds_.erase(part);
    rebuild_stages();
    return core::Result<void>::ok();
}

bool StagingList::is_staged(const PartIndex part) const noexcept {
    return assignments_.find(part) != assignments_.end();
}

u8 StagingList::stage_of(const PartIndex part) const noexcept {
    const auto it = assignments_.find(part);
    return it == assignments_.end() ? kUnstaged : it->second;
}

u8 StagingList::stage_count() const noexcept { return static_cast<u8>(stages_.size()); }

u8 StagingList::auto_assign(const VehicleAssembly& asmbl) {
    // Collect live decouplers with their world Y to compute per-part depth.
    struct DecouplerInfo {
        PartIndex index;
        f32 y;
    };
    std::vector<DecouplerInfo> decouplers;
    for (std::size_t i = 0; i < asmbl.parts().size(); ++i) {
        const Part* p = asmbl.part(static_cast<PartIndex>(i));
        if (!p) {
            continue;
        }
        const PartTemplate* tmpl = find_template(p->tmpl);
        if (tmpl->category == PartCategory::Decoupler) {
            decouplers.push_back(
                {static_cast<PartIndex>(i), asmbl.part_center_wu(static_cast<PartIndex>(i))[1]});
        }
    }

    assignments_.clear();
    action_kinds_.clear();
    for (std::size_t i = 0; i < asmbl.parts().size(); ++i) {
        const Part* p = asmbl.part(static_cast<PartIndex>(i));
        if (!p) {
            continue;
        }
        const PartTemplate* tmpl = find_template(p->tmpl);
        if (!is_stageable(tmpl->category)) {
            continue;
        }
        const f32 y = asmbl.part_center_wu(static_cast<PartIndex>(i))[1];
        u32 depth = 0;
        for (const DecouplerInfo& decoupler : decouplers) {
            if (decoupler.y < y - 1e-3f) {
                ++depth;
            }
        }
        const u32 stage = depth * 2 + (tmpl->category == PartCategory::Decoupler ? 1u : 0u);
        const u8 clamped = static_cast<u8>(std::min(stage, static_cast<u32>(kMaxStages - 1)));
        const PartIndex index = static_cast<PartIndex>(i);
        assignments_[index] = clamped;
        action_kinds_[index] = action_for(tmpl->category);
    }
    rebuild_stages();
    return static_cast<u8>(stages_.size());
}

core::Result<StageFireResult> StagingList::activate_next_stage(VehicleAssembly& asmbl) {
    if (cursor_ >= stages_.size()) {
        return core::Result<StageFireResult>::failure(ErrorCode::InvalidArgument,
                                                      "staging list exhausted");
    }
    const Stage stage = stages_[cursor_];
    StageFireResult result;

    // 1) Decouplers: cut and split; the pod-bearing component is adopted.
    std::vector<u32> decouplers;
    for (const StageActionItem& item : stage.items) {
        if (item.action == StageAction::FireDecoupler) {
            decouplers.push_back(item.part);
        }
    }
    if (!decouplers.empty()) {
        std::vector<VehicleAssembly> components = asmbl.split_at_decouplers(decouplers);
        if (components.empty()) {
            return core::Result<StageFireResult>::failure(ErrorCode::InvalidArgument,
                                                          "split produced no components");
        }
        // Choose the pod-bearing component; deterministic fallback = lowest indices.
        std::size_t vehicle = 0;
        bool vehicle_found = false;
        for (std::size_t c = 0; c < components.size(); ++c) {
            for (std::size_t i = 0; i < components[c].parts().size(); ++i) {
                const PartTemplate* tmpl = components[c].template_of(static_cast<PartIndex>(i));
                if (tmpl && tmpl->category == PartCategory::CommandPod) {
                    vehicle = c;
                    vehicle_found = true;
                    break;
                }
            }
            if (vehicle_found) {
                break;
            }
        }
        for (std::size_t c = 0; c < components.size(); ++c) {
            if (c == vehicle) {
                continue;
            }
            result.detached.push_back(std::move(components[c]));
        }
        asmbl.adopt(std::move(components[vehicle]));
    }

    // 2) Fairings: tombstone whatever is still part of the vehicle.
    std::vector<u32> fairings;
    for (const StageActionItem& item : stage.items) {
        if (item.action == StageAction::JettisonFairing) {
            fairings.push_back(item.part);
        }
    }
    if (!fairings.empty()) {
        const core::Result<u32> dropped = asmbl.jettison_fairings(fairings);
        if (!dropped.has_value()) {
            return core::Result<StageFireResult>::failure(ErrorCode::InvalidArgument,
                                                          "jettison failed during staging");
        }
    }

    // 3) Engines: record ignitions (M8 owns the dynamics).
    result.fired = stage.items;  // already in firing order
    ++cursor_;
    return core::Result<StageFireResult>::ok(std::move(result));
}

void StagingList::clear_all() noexcept {
    assignments_.clear();
    action_kinds_.clear();
    stages_.clear();
    cursor_ = 0;
}

}  // namespace aegis::parts
