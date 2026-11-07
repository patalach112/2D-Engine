// Aegis2D — vehicle assembly implementation (Milestone 7).
#include <aegis/parts/vehicle_assembly.hpp>

#include <aegis/core/assert.hpp>
#include <aegis/core/log.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "detail_geom.inc"

namespace aegis::parts {

using core::ErrorCode;

u64 hash_cell(const i16 gx, const i16 gy) noexcept {
    return (static_cast<u64>(static_cast<u16>(gx)) << 32) | static_cast<u64>(static_cast<u16>(gy));
}

std::pair<u8, u8> rotated_dims(const u8 w, const u8 h, const u8 rot) noexcept {
    return (rot_of(rot) % 2 == 1) ? std::pair{h, w} : std::pair{w, h};
}

Footprint footprint_of(const u8 w, const u8 h, const u8 rot) noexcept {
    Footprint out;
    const auto [rw, rh] = rotated_dims(w, h, rot);
    out.w = rw;
    out.h = rh;

    const f32 cx = static_cast<f32>(w) * 0.5f;
    const f32 cy = static_cast<f32>(h) * 0.5f;

    detail::Vec2 rel[kMaxCellsPerPart];
    u8 count = 0;
    f32 min_x = std::numeric_limits<f32>::max();
    f32 min_y = std::numeric_limits<f32>::max();
    for (u8 y = 0; y < h; ++y) {
        for (u8 x = 0; x < w; ++x) {
            detail::Vec2 v{static_cast<f32>(x) + 0.5f - cx, static_cast<f32>(y) + 0.5f - cy};
            v = detail::rotate_vec(v, rot);
            rel[count++] = v;
            min_x = std::min(min_x, v.x);
            min_y = std::min(min_y, v.y);
        }
    }
    out.count = count;
    for (u8 i = 0; i < count; ++i) {
        out.cells[i] = {static_cast<i16>(std::lround(rel[i].x - min_x)),
                        static_cast<i16>(std::lround(rel[i].y - min_y))};
    }
    return out;
}

std::pair<std::array<f32, 2>, std::array<f32, 2>> transform_node(const NodeDef& node,
                                                                const u8 w, const u8 h,
                                                                const u8 rot) noexcept {
    const auto [rw, rh] = rotated_dims(w, h, rot);
    const f32 cx = static_cast<f32>(w) * 0.5f;
    const f32 cy = static_cast<f32>(h) * 0.5f;

    detail::Vec2 p{node.x - cx, node.y - cy};
    p = detail::rotate_vec(p, rot);
    p.x += static_cast<f32>(rw) * 0.5f;
    p.y += static_cast<f32>(rh) * 0.5f;

    const detail::Vec2 n = detail::rotate_vec(detail::Vec2{node.nx, node.ny}, rot);
    return {{{p.x, p.y}}, {{n.x, n.y}}};
}

namespace {

const NodeDef* find_node(const PartTemplate& tmpl, const NodeId id) noexcept {
    if (id >= tmpl.nodes.size()) {
        return nullptr;
    }
    return &tmpl.nodes[id];
}

bool node_types_compatible(const NodeType a, const NodeType b) noexcept {
    return nodes_compatible(a, b);
}

constexpr f32 kNodeEps = 1e-3f;

bool near(const f32 a, const f32 b) noexcept { return std::fabs(a - b) <= 1e-3f; }
}  // namespace

const Part* VehicleAssembly::part(const PartIndex index) const noexcept {
    if (index >= parts_.size()) {
        return nullptr;
    }
    const Part& p = parts_[index];
    return p.tmpl == kInvalidTemplate ? nullptr : &p;
}

const PartTemplate* VehicleAssembly::template_of(const PartIndex index) const noexcept {
    const Part* p = part(index);
    return p ? find_template(p->tmpl) : nullptr;
}

bool VehicleAssembly::is_alive(const PartIndex index) const noexcept { return part(index) != nullptr; }

std::array<f32, 2> VehicleAssembly::part_center_wu(const PartIndex index) const noexcept {
    const Part* p = part(index);
    if (!p) {
        return {0.0f, 0.0f};
    }
    const PartTemplate* tmpl = find_template(p->tmpl);
    const auto [rw, rh] = rotated_dims(tmpl->w, tmpl->h, p->rot);
    return {(static_cast<f32>(p->gx) + static_cast<f32>(rw) * 0.5f) * kCellWu,
            (static_cast<f32>(p->gy) + static_cast<f32>(rh) * 0.5f) * kCellWu};
}

const MassProperties& VehicleAssembly::mass_properties() const noexcept {
    if (mass_dirty_) {
        rebuild_mass_properties();
    }
    return mass_cache_;
}

void VehicleAssembly::rebuild_mass_properties() const noexcept {
    MassProperties mp;
    f64 sum_mass = 0.0;
    f64 moment_x = 0.0, moment_y = 0.0;
    f32 dry = 0.0f, prop = 0.0f;

    for (const Part& part : parts_) {
        if (part.tmpl == kInvalidTemplate) {
            continue;
        }
        const PartTemplate* tmpl = find_template(part.tmpl);
        const f64 mass = static_cast<f64>(tmpl->dry_mass) + static_cast<f64>(part.propellant);
        dry += tmpl->dry_mass;
        prop += part.propellant;
        sum_mass += mass;

        const std::array<f32, 2> center = part_center_wu(static_cast<PartIndex>(&part - parts_.data()));
        moment_x += mass * static_cast<f64>(center[0]);
        moment_y += mass * static_cast<f64>(center[1]);
    }

    if (sum_mass > 0.0) {
        mp.center_of_mass = {static_cast<f32>(moment_x / sum_mass), static_cast<f32>(moment_y / sum_mass)};
    }

    f64 inertia = 0.0;
    for (const Part& part : parts_) {
        if (part.tmpl == kInvalidTemplate) {
            continue;
        }
        const PartTemplate* tmpl = find_template(part.tmpl);
        const f64 mass = static_cast<f64>(tmpl->dry_mass) + static_cast<f64>(part.propellant);
        const std::array<f32, 2> center = part_center_wu(static_cast<PartIndex>(&part - parts_.data()));
        const f64 dx = static_cast<f64>(center[0]) - static_cast<f64>(mp.center_of_mass[0]);
        const f64 dy = static_cast<f64>(center[1]) - static_cast<f64>(mp.center_of_mass[1]);
        inertia += mass * (dx * dx + dy * dy);
    }

    mp.dry_mass = dry;
    mp.propellant_mass = prop;
    mp.total_mass = dry + prop;
    mp.inertia_izz = static_cast<f32>(inertia);

    mass_cache_ = mp;
    mass_dirty_ = false;
}

core::Result<PartIndex> VehicleAssembly::place_unchecked(const TemplateId tmpl_id, const i16 gx,
                                                         const i16 gy, const u8 rot) noexcept {
    const PartTemplate* tmpl = find_template(tmpl_id);
    if (!tmpl) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid part template id");
    }
    if (alive_count_ >= kMaxParts) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "assembly part limit reached");
    }

    Part part;
    part.tmpl = tmpl_id;
    part.gx = gx;
    part.gy = gy;
    part.rot = rot;
    part.fuel_type = tmpl->tank.fuel_type;
    part.propellant = tmpl->tank.propellant;

    const PartIndex index = static_cast<PartIndex>(parts_.size());
    parts_.push_back(part);

    const Footprint foot = footprint_of(tmpl->w, tmpl->h, rot);
    for (u8 i = 0; i < foot.count; ++i) {
        occupancy_[hash_cell(static_cast<i16>(gx + foot.cells[i].first),
                             static_cast<i16>(gy + foot.cells[i].second))] = index;
    }
    ++alive_count_;
    mass_dirty_ = true;
    return core::Result<PartIndex>::ok(index);
}

core::Result<PartIndex> VehicleAssembly::add_root(const TemplateId tmpl, const i16 gx, const i16 gy,
                                                  const u8 rot) {
    const PartTemplate* tmpl_def = find_template(tmpl);
    if (!tmpl_def) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid part template id");
    }
    const Footprint foot = footprint_of(tmpl_def->w, tmpl_def->h, rot);
    for (u8 i = 0; i < foot.count; ++i) {
        const auto it = occupancy_.find(hash_cell(static_cast<i16>(gx + foot.cells[i].first),
                                                  static_cast<i16>(gy + foot.cells[i].second)));
        if (it != occupancy_.end()) {
            return core::Result<PartIndex>::failure(ErrorCode::AlreadyExists,
                                                    "footprint would overlap occupied cells");
        }
    }
    return place_unchecked(tmpl, gx, gy, rot);
}

core::Result<PartIndex> VehicleAssembly::snap(const PartIndex parent, const NodeId parent_node,
                                              const TemplateId child, const NodeId child_node,
                                              const u8 rot) {
    const Part* parent_part = part(parent);
    if (!parent_part) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid parent part");
    }
    const PartTemplate* parent_tmpl = find_template(parent_part->tmpl);
    const PartTemplate* child_tmpl = find_template(child);
    if (!child_tmpl) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid child template id");
    }
    const NodeDef* p_node = find_node(*parent_tmpl, parent_node);
    const NodeDef* c_node = find_node(*child_tmpl, child_node);
    if (!p_node) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid parent node id");
    }
    if (!c_node) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid child node id");
    }

    if (!node_types_compatible(p_node->type, c_node->type)) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument,
                                                "node types are not compatible");
    }

    const auto [p_local, p_normal] =
        transform_node(*p_node, parent_tmpl->w, parent_tmpl->h, parent_part->rot);
    const f32 parent_wx = static_cast<f32>(parent_part->gx) + p_local[0];
    const f32 parent_wy = static_cast<f32>(parent_part->gy) + p_local[1];

    const auto [c_local, c_normal] = transform_node(*c_node, child_tmpl->w, child_tmpl->h, rot);

    const f32 dot = p_normal[0] * c_normal[0] + p_normal[1] * c_normal[1];
    if (dot > -0.99f) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument,
                                                "node normals do not oppose");
    }

    const f32 ox = parent_wx - c_local[0];
    const f32 oy = parent_wy - c_local[1];
    const f32 round_x = std::round(ox);
    const f32 round_y = std::round(oy);
    if (std::fabs(ox - round_x) > 1e-3f || std::fabs(oy - round_y) > 1e-3f) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument,
                                                "placement would not align to the grid");
    }

    const i16 child_gx = static_cast<i16>(round_x);
    const i16 child_gy = static_cast<i16>(round_y);

    return add_root(child, child_gx, child_gy, rot);
}

core::Result<PartIndex> VehicleAssembly::mirror_snap(const PartIndex parent, const NodeId parent_node,
                                                     const TemplateId child, const NodeId child_node,
                                                     const u8 rot, const Axis axis,
                                                     const f32 axis_wu) {
    const Part* parent_part = part(parent);
    if (!parent_part) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid parent part");
    }
    const PartTemplate* parent_tmpl = find_template(parent_part->tmpl);
    const NodeDef* p_node = find_node(*parent_tmpl, parent_node);
    if (!p_node) {
        return core::Result<PartIndex>::failure(ErrorCode::InvalidArgument, "invalid parent node id");
    }

    const auto [p_local, p_normal] =
        transform_node(*p_node, parent_tmpl->w, parent_tmpl->h, parent_part->rot);
    const f32 pwx = static_cast<f32>(parent_part->gx) + p_local[0];
    const f32 pwy = static_cast<f32>(parent_part->gy) + p_local[1];

    const f32 axis_cells = axis_wu / kCellWu;
    f32 mirror_x = pwx, mirror_y = pwy, mnor_x = p_normal[0], mnor_y = p_normal[1];
    if (axis == Axis::Vertical) {
        mirror_x = 2.0f * axis_cells - pwx;
        mnor_x = -p_normal[0];
    } else {
        mirror_y = 2.0f * axis_cells - pwy;
        mnor_y = -p_normal[1];
    }

    // Find the mirror counterpart node on the SAME part.
    std::optional<NodeId> mirror_id;
    for (const NodeDef& candidate : parent_tmpl->nodes) {
        const auto [c_local, c_normal] =
            transform_node(candidate, parent_tmpl->w, parent_tmpl->h, parent_part->rot);
        const f32 cwx = static_cast<f32>(parent_part->gx) + c_local[0];
        const f32 cwy = static_cast<f32>(parent_part->gy) + c_local[1];
        if (near(cwx, mirror_x) && near(cwy, mirror_y) && near(c_normal[0], mnor_x) &&
            near(c_normal[1], mnor_y)) {
            mirror_id = candidate.id;
            break;
        }
    }
    if (!mirror_id.has_value()) {
        return core::Result<PartIndex>::failure(ErrorCode::NotFound,
                                                "no mirrored counterpart node on the parent part");
    }

    const u8 mirrored_rot =
        axis == Axis::Vertical ? mirror_rot_vertical(rot) : mirror_rot_horizontal(rot);
    return snap(parent, *mirror_id, child, child_node, mirrored_rot);
}

std::vector<std::pair<u32, u32>> VehicleAssembly::adjacency() const {
    std::vector<std::pair<u32, u32>> edges;

    for (std::size_t i = 0; i < parts_.size(); ++i) {
        const Part& part = parts_[i];
        if (part.tmpl == kInvalidTemplate) {
            continue;
        }
        const PartTemplate* tmpl = find_template(part.tmpl);
        for (const NodeDef& node : tmpl->nodes) {
            const auto [local, normal] = transform_node(node, tmpl->w, tmpl->h, part.rot);
            const f32 wx = (static_cast<f32>(part.gx) + local[0]);
            const f32 wy = (static_cast<f32>(part.gy) + local[1]);

            // Neighbour cell: step half a cell along the node normal.
            const i16 cell_x = static_cast<i16>(std::floor(wx + normal[0] * 0.5f));
            const i16 cell_y = static_cast<i16>(std::floor(wy + normal[1] * 0.5f));
            const auto it = occupancy_.find(hash_cell(cell_x, cell_y));
            if (it == occupancy_.end() || it->second == i) {
                continue;
            }
            const PartIndex other = it->second;
            const PartTemplate* other_tmpl = find_template(parts_[other].tmpl);

            for (const NodeDef& other_node : other_tmpl->nodes) {
                const auto [other_local, other_normal] =
                    transform_node(other_node, other_tmpl->w, other_tmpl->h, parts_[other].rot);
                const f32 owx = static_cast<f32>(parts_[other].gx) + other_local[0];
                const f32 owy = static_cast<f32>(parts_[other].gy) + other_local[1];
                if (near(owx, wx) && near(owy, wy) && near(other_normal[0], -normal[0]) &&
                    near(other_normal[1], -normal[1])) {
                    const u32 a = static_cast<u32>(i), b = other;
                    edges.emplace_back(std::min(a, b), std::max(a, b));
                    break;
                }
            }
        }
    }

    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    return edges;
}

core::Result<u32> VehicleAssembly::remove(const PartIndex index) {
    if (!is_alive(index)) {
        return core::Result<u32>::failure(ErrorCode::InvalidArgument, "invalid part index");
    }

    const std::vector<std::pair<u32, u32>> edges = adjacency();

    // Cut everything incident to `index`, find which resulting components lose
    // all connectivity to any command pod, tombstone those.
    std::vector<std::vector<u32>> components;
    {
        const u32 n = static_cast<u32>(parts_.size());
        std::vector<u32> parent_dsu(n);
        for (u32 i = 0; i < n; ++i) {
            parent_dsu[i] = i;
        }
        const auto find = [&](u32 x) {
            while (parent_dsu[x] != x) {
                parent_dsu[x] = parent_dsu[parent_dsu[x]];
                x = parent_dsu[x];
            }
            return x;
        };
        for (const auto& [a, b] : edges) {
            if (a == index || b == index) {
                continue;  // cut at the removed part
            }
            parent_dsu[find(a)] = find(b);
        }
        std::unordered_map<u32, std::vector<u32>> by_root;
        for (u32 i = 0; i < n; ++i) {
            if (parts_[i].tmpl != kInvalidTemplate) {
                by_root[find(i)].push_back(i);
            }
        }
        for (auto& [root, members] : by_root) {
            components.push_back(std::move(members));
        }
        std::sort(components.begin(), components.end(),
                  [](const std::vector<u32>& a, const std::vector<u32>& b) {
                      return a.front() < b.front();
                  });
    }

    const auto has_pod = [&](const std::vector<u32>& members) {
        for (const u32 member : members) {
            const PartTemplate* tmpl = find_template(parts_[member].tmpl);
            if (tmpl && tmpl->category == PartCategory::CommandPod) {
                return true;
            }
        }
        return false;
    };

    u32 removed = 0;
    const auto tombstone = [&](u32 member) {
        const Part& p = parts_[member];
        const PartTemplate* tmpl = find_template(p.tmpl);
        const Footprint foot = footprint_of(tmpl->w, tmpl->h, p.rot);
        for (u8 i = 0; i < foot.count; ++i) {
            occupancy_.erase(hash_cell(static_cast<i16>(p.gx + foot.cells[i].first),
                                       static_cast<i16>(p.gy + foot.cells[i].second)));
        }
        parts_[member].tmpl = kInvalidTemplate;
        ++removed;
        --alive_count_;
    };

    // `index` itself is removed unconditionally; its orphaned tree goes with it.
    const u32 target_root = index;  // components not containing a pod are removed
    for (const std::vector<u32>& members : components) {
        bool contains_target = false;
        for (const u32 member : members) {
            if (member == target_root) {
                contains_target = true;
                break;
            }
        }
        if (contains_target) {
            for (const u32 member : members) {
                tombstone(member);
            }
            continue;
        }
        if (!has_pod(members)) {
            for (const u32 member : members) {
                tombstone(member);
            }
        }
    }

    mass_dirty_ = true;
    return core::Result<u32>::ok(removed);
}

std::vector<AssemblyIssue> VehicleAssembly::validate() const {
    std::vector<AssemblyIssue> issues;

    bool any_pod = false;
    for (const Part& part : parts_) {
        if (part.tmpl == kInvalidTemplate) {
            continue;
        }
        const PartTemplate* tmpl = find_template(part.tmpl);
        if (tmpl->category == PartCategory::CommandPod) {
            any_pod = true;
            break;
        }
    }
    if (!any_pod) {
        issues.push_back({AssemblyIssue::Kind::NoCommandPod, kInvalidPart});
        return issues;  // reachability is meaningless without a pod
    }

    // Reachability from every pod across live adjacency.
    const u32 n = static_cast<u32>(parts_.size());
    std::vector<char> reached(n, 0);
    std::vector<std::vector<u32>> graph(n);
    for (const auto& [a, b] : adjacency()) {
        graph[a].push_back(b);
        graph[b].push_back(a);
    }
    std::vector<u32> queue;
    for (u32 i = 0; i < n; ++i) {
        if (parts_[i].tmpl == kInvalidTemplate) {
            continue;
        }
        const PartTemplate* tmpl = find_template(parts_[i].tmpl);
        if (tmpl->category == PartCategory::CommandPod) {
            reached[i] = 1;
            queue.push_back(i);
        }
    }
    while (!queue.empty()) {
        const u32 current = queue.back();
        queue.pop_back();
        for (const u32 next : graph[current]) {
            if (!reached[next]) {
                reached[next] = 1;
                queue.push_back(next);
            }
        }
    }
    for (u32 i = 0; i < n; ++i) {
        if (parts_[i].tmpl != kInvalidTemplate && !reached[i]) {
            issues.push_back({AssemblyIssue::Kind::UnreachablePart, i});
        }
    }
    return issues;
}

void VehicleAssembly::adopt(VehicleAssembly&& other) noexcept {
    *this = std::move(other);
}

core::Result<u32> VehicleAssembly::jettison_fairings(const std::vector<u32>& parts_to_drop) noexcept {
    u32 dropped = 0;
    for (const u32 id : parts_to_drop) {
        const Part* p = part(id);
        if (!p) {
            continue;  // already gone (e.g. travelled with a detached component)
        }
        const PartTemplate* tmpl = find_template(p->tmpl);
        if (!tmpl || tmpl->category != PartCategory::Fairing) {
            return core::Result<u32>::failure(ErrorCode::InvalidArgument,
                                              "jettison_fairings expects fairing parts only");
        }
        const Part& part_ref = parts_[id];
        const Footprint foot = footprint_of(tmpl->w, tmpl->h, part_ref.rot);
        for (u8 i = 0; i < foot.count; ++i) {
            occupancy_.erase(hash_cell(static_cast<i16>(part_ref.gx + foot.cells[i].first),
                                       static_cast<i16>(part_ref.gy + foot.cells[i].second)));
        }
        parts_[id].tmpl = kInvalidTemplate;
        --alive_count_;
        ++dropped;
    }
    mass_dirty_ = true;
    return core::Result<u32>::ok(dropped);
}

core::Result<f32> VehicleAssembly::consume_propellant(const PartIndex tank,
                                                       const f32 kg) noexcept {
    const Part* existing = part(tank);
    if (!existing) {
        return core::Result<f32>::failure(ErrorCode::InvalidArgument, "consume_propellant: unknown or dead tank");
    }
    if (!(kg >= 0.0f)) {
        return core::Result<f32>::failure(ErrorCode::InvalidArgument,
                                          "consume_propellant: drain must be non-negative");
    }
    const PartTemplate* tmpl = find_template(existing->tmpl);
    if (!tmpl || tmpl->category != PartCategory::FuelTank) {
        return core::Result<f32>::failure(ErrorCode::InvalidArgument,
                                          "consume_propellant: part is not a fuel tank");
    }
    Part& p = parts_[tank];
    const f32 drained = std::min(kg, p.propellant);
    p.propellant -= drained;
    mass_dirty_ = true;
    return core::Result<f32>::ok(drained);
}

std::vector<VehicleAssembly> VehicleAssembly::split_members(const std::vector<u32>& members) const {
    std::vector<u32> unique;
    unique.reserve(members.size());
    for (const u32 id : members) {
        if (id < parts_.size() && is_alive(id)) {
            unique.push_back(id);
        }
    }
    if (unique.empty()) {
        return {};
    }
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    std::vector<VehicleAssembly> out;
    out.reserve(2);

    VehicleAssembly remaining;
    remaining.parts_ = parts_;  // copies tombstones as well
    VehicleAssembly detached;
    detached.parts_ = parts_;

    std::vector<u32> member_mask(parts_.size(), 0);
    for (const u32 member : unique) {
        member_mask[member] = 1;
    }

    const auto build = [&](VehicleAssembly& child, const bool keep_members) {
        child.occupancy_.clear();
        for (std::size_t i = 0; i < child.parts_.size(); ++i) {
            Part& part = child.parts_[i];
            if (part.tmpl == kInvalidTemplate) {
                continue;
            }
            if (member_mask[i] == (keep_members ? 1u : 0u)) {
                const PartTemplate* tmpl = find_template(part.tmpl);
                const Footprint foot = footprint_of(tmpl->w, tmpl->h, part.rot);
                for (u8 c = 0; c < foot.count; ++c) {
                    child.occupancy_[hash_cell(static_cast<i16>(part.gx + foot.cells[c].first),
                                               static_cast<i16>(part.gy + foot.cells[c].second))] =
                        static_cast<PartIndex>(i);
                }
            } else {
                part.tmpl = kInvalidTemplate;  // gap tombstone
            }
        }
        child.alive_count_ = 0;
        for (const Part& part : child.parts_) {
            if (part.tmpl != kInvalidTemplate) {
                ++child.alive_count_;
            }
        }
        child.mass_dirty_ = true;
    };

    build(remaining, /*keep_members=*/false);
    build(detached, /*keep_members=*/true);
    out.push_back(std::move(remaining));
    out.push_back(std::move(detached));
    return out;
}

std::vector<VehicleAssembly> VehicleAssembly::split_at_decouplers(
    const std::vector<u32>& decoupler_parts) const {
    // NOTE: declared to return assemblies; invalid indices panic-free filter.
    std::vector<char> is_decoupler(parts_.size(), 0);
    for (const u32 id : decoupler_parts) {
        if (id < parts_.size() && is_alive(id)) {
            is_decoupler[id] = 1;
        }
    }

    const u32 n = static_cast<u32>(parts_.size());
    std::vector<u32> parent_dsu(n);
    for (u32 i = 0; i < n; ++i) {
        parent_dsu[i] = i;
    }
    const auto find = [&](u32 x) {
        while (parent_dsu[x] != x) {
            parent_dsu[x] = parent_dsu[parent_dsu[x]];
            x = parent_dsu[x];
        }
        return x;
    };

    const std::vector<std::pair<u32, u32>> edges = adjacency();
    for (const auto& [a, b] : edges) {
        if (is_decoupler[a] || is_decoupler[b]) {
            continue;  // cut edge at the fired decoupler
        }
        parent_dsu[find(a)] = find(b);
    }

    // Re-attach each (now isolated) decoupler carcass to a neighbour component:
    // prefer the part reachable through its bottom node (the lower stage), then
    // any other surviving neighbour; lowest original index wins ties.
    for (u32 decoupler = 0; decoupler < n; ++decoupler) {
        if (!is_decoupler[decoupler]) {
            continue;
        }
        std::vector<u32> neighbours;
        for (const auto& [a, b] : edges) {
            if (a == decoupler && !is_decoupler[b]) {
                neighbours.push_back(b);
            } else if (b == decoupler && !is_decoupler[a]) {
                neighbours.push_back(a);
            }
        }
        if (neighbours.empty()) {
            continue;  // decoupled into nothing (both neighbours fired too)
        }
        // Prefer the neighbour sitting across the separator's bottom node (the
        // lower stage); fall back to the lowest original index (deterministic).
        u32 preferred = std::numeric_limits<u32>::max();
        const PartTemplate* decoupler_tmpl = find_template(parts_[decoupler].tmpl);
        const Part& decoupler_part = parts_[decoupler];
        for (const NodeDef& node : decoupler_tmpl->nodes) {
            const auto [local, normal] = transform_node(node, decoupler_tmpl->w, decoupler_tmpl->h,
                                                        decoupler_part.rot);
            if (normal[1] >= 0.0f) {
                continue;  // not facing downward
            }
            const f32 wx = static_cast<f32>(decoupler_part.gx) + local[0];
            const f32 wy = static_cast<f32>(decoupler_part.gy) + local[1];
            const i16 cell_x = static_cast<i16>(std::floor(wx + normal[0] * 0.5f));
            const i16 cell_y = static_cast<i16>(std::floor(wy + normal[1] * 0.5f));
            const auto it = occupancy_.find(hash_cell(cell_x, cell_y));
            if (it != occupancy_.end() && it->second != decoupler &&
                std::find(neighbours.begin(), neighbours.end(), it->second) != neighbours.end()) {
                preferred = it->second;
                break;
            }
        }
        if (preferred != std::numeric_limits<u32>::max()) {
            parent_dsu[find(decoupler)] = find(preferred);
        } else {
            std::sort(neighbours.begin(), neighbours.end());
            parent_dsu[find(decoupler)] = find(neighbours.front());
        }
    }

    std::unordered_map<u32, std::vector<u32>> by_root;
    for (u32 i = 0; i < n; ++i) {
        if (parts_[i].tmpl != kInvalidTemplate) {
            by_root[find(i)].push_back(i);
        }
    }
    std::vector<std::vector<u32>> components;
    components.reserve(by_root.size());
    for (auto& [root, members] : by_root) {
        components.push_back(std::move(members));
    }
    std::sort(components.begin(), components.end(),
              [](const std::vector<u32>& a, const std::vector<u32>& b) { return a.front() < b.front(); });

    std::vector<VehicleAssembly> out;
    out.reserve(components.size());
    for (const std::vector<u32>& members : components) {
        VehicleAssembly child_asm;
        // Keep original indices: non-members become tombstoned gaps so that
        // PartIndex-keyed assignments (staging) survive the split untouched.
        child_asm.parts_ = parts_;  // copies tombstones for dead parts as well
        std::vector<u32> member_mask(parts_.size(), 0);
        for (const u32 member : members) {
            member_mask[member] = 1;
        }
        for (std::size_t i = 0; i < child_asm.parts_.size(); ++i) {
            if (child_asm.parts_[i].tmpl == kInvalidTemplate) {
                continue;
            }
            if (member_mask[i]) {
                const Part& part = child_asm.parts_[i];
                const PartTemplate* tmpl = find_template(part.tmpl);
                const Footprint foot = footprint_of(tmpl->w, tmpl->h, part.rot);
                for (u8 c = 0; c < foot.count; ++c) {
                    child_asm.occupancy_[hash_cell(static_cast<i16>(part.gx + foot.cells[c].first),
                                                   static_cast<i16>(part.gy + foot.cells[c].second))] =
                        static_cast<PartIndex>(i);
                }
            } else {
                child_asm.parts_[i].tmpl = kInvalidTemplate;  // gap tombstone
            }
        }
        child_asm.alive_count_ = static_cast<u32>(members.size());
        child_asm.mass_dirty_ = true;
        out.push_back(std::move(child_asm));
    }
    return out;
}

void VehicleAssembly::clear() noexcept {
    parts_.clear();
    occupancy_.clear();
    alive_count_ = 0;
    mass_cache_ = MassProperties{};
    mass_dirty_ = true;
}

namespace {

const NodeDef* first_node_of_type(const PartTemplate& tmpl, const NodeType type) noexcept {
    for (const NodeDef& node : tmpl.nodes) {
        if (node.type == type) {
            return &node;
        }
    }
    return nullptr;
}

}  // namespace

VehicleAssembly make_demo_rocket() noexcept {
    VehicleAssembly assembly;
    // All-1-wide stack: snap() requires integer grid alignment, which mixed
    // parity (1-wide on a 2-wide core) cannot satisfy — 1-wide nodes land on
    // half-cell offsets. Sparrow / Long-2 / decoupler / Sparrow / Short-1 / pod
    // keeps the whole chain on the integer grid.
    const TemplateId liftoff = 4, long2 = 1, decoupler = 7, upper = 4, short1 = 3, pod = 0;
    const core::Result<PartIndex> root = assembly.add_root(liftoff, 0, 0, 0);
    if (!root.has_value()) {
        return assembly;  // cannot fail today, but stay total
    }
    const PartIndex engine = root.value();

    auto stack = [&assembly](const PartIndex parent, const TemplateId child) -> core::Result<PartIndex> {
        const PartTemplate* parent_tmpl = assembly.template_of(parent);
        const PartTemplate* child_tmpl = find_template(child);
        if (parent_tmpl == nullptr || child_tmpl == nullptr) {
            return core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "unknown template");
        }
        const NodeDef* p_top = first_node_of_type(*parent_tmpl, NodeType::Top);
        const NodeDef* c_bottom = first_node_of_type(*child_tmpl, NodeType::Bottom);
        if (p_top == nullptr || c_bottom == nullptr) {
            return core::Result<PartIndex>::failure(core::ErrorCode::InvalidArgument, "missing stack node");
        }
        return assembly.snap(parent, p_top->id, child, c_bottom->id, 0);
    };

    const core::Result<PartIndex> tank = stack(engine, long2);
    if (!tank.has_value()) return assembly;
    const core::Result<PartIndex> ring = stack(tank.value(), decoupler);
    if (!ring.has_value()) return assembly;
    const core::Result<PartIndex> second = stack(ring.value(), upper);
    if (!second.has_value()) return assembly;
    const core::Result<PartIndex> upper_tank = stack(second.value(), short1);
    if (!upper_tank.has_value()) return assembly;
    const core::Result<PartIndex> pod_index = stack(upper_tank.value(), pod);
    if (!pod_index.has_value()) return assembly;
    return assembly;
}

}  // namespace aegis::parts
