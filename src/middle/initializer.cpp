// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/initializer.hpp"

#include "common/diagnostic.hpp"
#include "frontend/ast.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cross::initializer {
namespace {

struct DeferredPosition {
    const Expr* origin{};
    std::uint64_t displacement{};
    bool operator==(const DeferredPosition&) const = default;
};
using Position = std::variant<std::uint64_t, DeferredPosition>;
using Path = std::vector<Position>;

std::optional<std::uint64_t> concrete(const Position& position) {
    if (const auto* value = std::get_if<std::uint64_t>(&position)) return *value;
    return {};
}

Position next_position(Position position) {
    if (auto* value = std::get_if<std::uint64_t>(&position)) ++*value;
    else ++std::get<DeferredPosition>(position).displacement;
    return position;
}

bool aggregate(const hir::Module& module, hir::TypeId type) {
    const auto kind = module.type(type).kind;
    return kind == hir::Type::Kind::Array ||
           kind == hir::Type::Kind::Record;
}

// The destinations of one brace list's entries as a prefix tree of the path
// steps below the list's own destination. An entry that initializes an
// aggregate from a value initializes it as a whole, and a later entry may
// replace a part of it. Any other overlap with an earlier entry is an error:
// a repeated path, a part before its whole or after a brace list, and a
// second member of one union.
class Destinations {
public:
    enum class Result { Added, Duplicate, Overlap };

    // Adds the steps of `path` from `depth` on; `unions[i]` tells whether
    // step i selects a union member. `whole` marks an aggregate value whose
    // parts later entries may replace. Sets `node` to the destination's node.
    Result add(const Path& path, const std::vector<bool>& unions, std::size_t depth,
               bool whole, std::uint32_t& node) {
        node = 0;
        for (auto index = depth; index < path.size(); ++index) {
            if (nodes_[node].terminal && !nodes_[node].whole) return Result::Overlap;
            const auto found = edges_.find(edge(node, path[index]));
            const unsigned same = found != edges_.end() ? 1U : 0U;
            if (unions[index] && nodes_[node].children > same) return Result::Overlap;
            if (found == edges_.end()) {
                for (; index < path.size(); ++index) {
                    const auto child = static_cast<std::uint32_t>(nodes_.size());
                    nodes_.push_back({});
                    ++nodes_[node].children;
                    edges_.emplace(edge(node, path[index]), child);
                    node = child;
                }
                nodes_[node].terminal = true;
                nodes_[node].whole = whole;
                return Result::Added;
            }
            node = found->second;
        }
        return nodes_[node].terminal ? Result::Duplicate : Result::Overlap;
    }

    // A base initializes the list's own destination as a whole.
    void base() { nodes_[0].terminal = nodes_[0].whole = true; }

    // The node an entry's path reaches from `node` through `position`.
    std::optional<std::uint32_t> child(std::uint32_t node, const Position& position) const {
        const auto found = edges_.find(edge(node, position));
        if (found == edges_.end()) return std::nullopt;
        return found->second;
    }
    // The children of `node` at concrete positions, in position order.
    std::vector<std::pair<std::uint64_t, std::uint32_t>> children(std::uint32_t node) const {
        std::vector<std::pair<std::uint64_t, std::uint32_t>> result;
        for (const auto& [edge, child] : edges_)
            if (edge.parent == node && !edge.origin) result.emplace_back(edge.value, child);
        std::sort(result.begin(), result.end());
        return result;
    }
    // Whether an entry initializes the whole object of `node`.
    bool terminal(std::uint32_t node) const { return nodes_[node].terminal; }
    // Whether no entry initializes a part of the object of `node`.
    bool childless(std::uint32_t node) const { return nodes_[node].children == 0; }

private:
    struct Node {
        bool terminal{};
        bool whole{};
        unsigned children{};
    };
    struct Edge {
        std::uint32_t parent{};
        std::uint64_t value{};
        const Expr* origin{};
        bool operator==(const Edge&) const = default;
    };
    struct EdgeHash {
        std::size_t operator()(const Edge& edge) const {
            return std::hash<std::uint64_t>{}(
                       (edge.value * 0x9e3779b97f4a7c15ULL) ^ edge.parent) ^
                   std::hash<const Expr*>{}(edge.origin);
        }
    };
    static Edge edge(std::uint32_t parent, const Position& position) {
        if (const auto* value = std::get_if<std::uint64_t>(&position)) return {parent, *value, nullptr};
        const auto& deferred = std::get<DeferredPosition>(position);
        return {parent, deferred.displacement, deferred.origin};
    }

    std::vector<Node> nodes_{Node{}};
    std::unordered_map<Edge, std::uint32_t, EdgeHash> edges_;
};

unsigned offset_alignment(unsigned natural, std::uint64_t offset) {
    if (offset == 0) return natural;
    unsigned actual = 1;
    while (actual <= natural / 2U &&
           (offset & (actual * 2U - 1U)) == 0) {
        actual *= 2U;
    }
    return actual;
}

class Planner {
public:
    Planner(const hir::Module& module, const TargetInfo& target,
            Diagnostics& diagnostics, bool dynamic_outer = false,
            std::span<const Expr* const> deferred_indices = {},
            bool types_only = false, std::span<const hir::TypeId> pending_extents = {})
        : module_(module), target_(target), diagnostics_(diagnostics),
          dynamic_outer_(dynamic_outer), deferred_indices_(deferred_indices),
          types_only_(types_only), pending_extents_(pending_extents) {}

    Plan run(const Expr& source, hir::TypeId type) {
        root_ = type;
        if (source.kind != Expr::Kind::AggregateInitializer) {
            error(source.location,
                               "aggregate initializer requires a brace list");
            result_.valid = false;
            return std::move(result_);
        }
        const auto offset = types_only_ ? std::nullopt : std::optional<std::uint64_t>{0};
        const auto* value = unbraced(source, type);
        if (value == &source) list(source, type, offset, {});
        else if (value) {
            if (dynamic_outer_ && module_.type(type).lanes == 0 && value->kind == Expr::Kind::String)
                result_.minimum_elements = value->string_value.size() + 1U;
            item(*value, {type, offset, {}, Position{std::uint64_t{0}}, std::nullopt, 0});
        }
        return std::move(result_);
    }

private:
    void error(SourceLocation location, std::string_view message) {
        if (result_.error_message.empty()) {
            result_.error_location = location;
            result_.error_message = message;
        }
        diagnostics_.error(location, message);
        result_.valid = false;
    }

    struct Selection {
        hir::TypeId type;
        std::optional<std::uint64_t> offset;
        Path path;
        Position direct_index;
        std::optional<unsigned> bit_width;
        unsigned bit_offset{};
    };

    // The initializer a brace list applies to a scalar or u8 array `type`:
    // the enclosed value, `source` itself, or null after a diagnostic.
    const Expr* unbraced(const Expr& source, hir::TypeId type) {
        const auto result = unbrace(source, module_, type);
        if (!result.value) {
            if (result.error) error(result.error_location, result.error);
            result_.valid = false;
        }
        return result.value;
    }

    void item(const Expr& value, const Selection& selection) {
        if (types_only_ || !deferred_indices_.empty())
            result_.type_items.emplace_back(&value, selection.type);
        if (!selection.offset) return;
        const auto natural_value =
            hir::layout_alignment(module_, selection.type, target_)
                .value_or(1);
        const auto natural = natural_value <=
                                     std::numeric_limits<unsigned>::max()
                                 ? static_cast<unsigned>(natural_value)
                                 : 1U;
        result_.items.push_back(
            {&value, selection.type, *selection.offset,
             offset_alignment(natural, *selection.offset),
             selection.bit_width, selection.bit_offset});
    }

    std::optional<Position> index_value(const Expr& expression) {
        if (std::find(deferred_indices_.begin(), deferred_indices_.end(), &expression) != deferred_indices_.end())
            return Position{DeferredPosition{&expression, 0}};
        const Expr* source = &expression;
        while (source->kind == Expr::Kind::Parenthesized && source->left) {
            source = source->left.get();
        }
        if (!source->evaluated_integer ||
            source->evaluated_integer->value.high != 0) {
            error(
                expression.location,
                "array initializer designator requires a nonnegative integer constant");
            result_.valid = false;
            return std::nullopt;
        }
        return Position{source->evaluated_integer->value.low};
    }

    std::optional<Selection> child(hir::TypeId parent,
                                   std::optional<std::uint64_t> parent_offset,
                                   Path path, Position position,
                                   SourceLocation location) {
        const auto& type = module_.type(parent);
        const auto index = concrete(position);
        if (type.kind == hir::Type::Kind::Array && type.element) {
            const bool dynamic = dynamic_outer_ && path.empty() &&
                                 type.lanes == 0;
            const bool pending = types_only_ &&
                std::find(pending_extents_.begin(), pending_extents_.end(), parent) != pending_extents_.end();
            const auto maximum_count = module_.address_bits >= 64
                ? std::numeric_limits<std::uint64_t>::max()
                : (std::uint64_t{1} << module_.address_bits) - 1U;
            // Required indices are nonnegative. Even an unresolved origin
            // cannot make an excessive following positional displacement fit.
            const auto minimum_index = index ? *index : std::get<DeferredPosition>(position).displacement;
            if ((!dynamic && !pending && (type.lanes == 0 || minimum_index >= type.lanes)) ||
                ((dynamic || pending) && minimum_index >= maximum_count)) {
                error(location,
                                   "array initializer designator is out of range");
                result_.valid = false;
                return std::nullopt;
            }
            const auto size = types_only_ ? std::nullopt : hir::layout_size(module_, *type.element, target_);
            if (!types_only_ && (!size || (index && (*index >
                             std::numeric_limits<std::uint64_t>::max() /
                                 *size ||
                (parent_offset && *parent_offset >
                    std::numeric_limits<std::uint64_t>::max() -
                        *index * *size))))) {
                error(location,
                                   "array initializer offset overflows target storage");
                result_.valid = false;
                return std::nullopt;
            }
            if (dynamic && index) {
                result_.minimum_elements = std::max<std::uint64_t>(
                    result_.minimum_elements,
                    *index + 1U);
            }
            if (dynamic && !index) result_.outer_extent_deferred = true;
            path.push_back(position);
            std::optional<std::uint64_t> offset;
            if (parent_offset && index && size) offset = *parent_offset + *index * *size;
            return Selection{*type.element,
                             offset, std::move(path), position, std::nullopt, 0};
        }
        if (type.kind == hir::Type::Kind::Record && type.record) {
            const auto& record = module_.record(*type.record);
            auto member = record.members.end();
            std::size_t logical_index{};
            for (auto candidate = record.members.begin();
                 candidate != record.members.end(); ++candidate) {
                if (!candidate->initialized()) continue;
                if (index && logical_index++ == *index) {
                    member = candidate;
                    break;
                }
            }
            if (member == record.members.end() ||
                (record.is_union && index != 0)) {
                error(location,
                                   "excess entry in aggregate initializer");
                result_.valid = false;
                return std::nullopt;
            }
            const auto raw_index = static_cast<std::size_t>(
                std::distance(record.members.begin(), member));
            path.push_back(static_cast<std::uint64_t>(raw_index));
            return Selection{member->type,
                             parent_offset ? std::optional(*parent_offset + member->offset) : std::nullopt,
                             std::move(path), position, member->bit_width,
                             member->bit_offset};
        }
        error(location,
                           "initializer designator requires an aggregate destination");
        result_.valid = false;
        return std::nullopt;
    }

    std::optional<Selection> designate(
        hir::TypeId parent, std::optional<std::uint64_t> parent_offset, Path path,
        const Expr::InitializerDesignator& designator) {
        const auto& type = module_.type(parent);
        if (designator.kind == Expr::InitializerDesignator::Kind::Index) {
            if (type.kind != hir::Type::Kind::Array || !designator.index) {
                error(
                    designator.location,
                    "array designator requires an array destination");
                result_.valid = false;
                return std::nullopt;
            }
            const auto index = index_value(*designator.index);
            if (!index) {
                return std::nullopt;
            }
            return child(parent, parent_offset, std::move(path),
                         *index,
                         designator.location);
        }
        if (type.kind != hir::Type::Kind::Record || !type.record) {
            error(designator.location,
                               "member designator requires a record destination");
            result_.valid = false;
            return std::nullopt;
        }
        const auto& record = module_.record(*type.record);
        auto member = std::find_if(
            record.members.begin(), record.members.end(),
            [&](const hir::RecordMember& candidate) {
                return candidate.member_name() == designator.member_name();
            });
        // A promoted member is designated through the embedded members.
        const auto* promoted = member == record.members.end()
            ? module_.promoted_member(*type.record, designator.member_name()) : nullptr;
        if (promoted && promoted->ambiguous) {
            error(designator.location, "member '" + designator.member +
                "' is ambiguous: embedded members at the same depth both have it");
            return std::nullopt;
        }
        if (promoted) member = record.members.begin() + promoted->path.front();
        if (member == record.members.end()) {
            error(
                designator.location,
                "record initializer has no member named '" +
                    designator.member + "'");
            result_.valid = false;
            return std::nullopt;
        }
        const auto& selected = promoted ? promoted->member : *member;
        if (promoted) path.insert(path.end(), promoted->path.begin(), promoted->path.end());
        else path.push_back(static_cast<std::uint64_t>(std::distance(record.members.begin(), member)));
        const auto logical_index = static_cast<std::size_t>(std::count_if(
            record.members.begin(), member,
            [](const hir::RecordMember& candidate) {
                return candidate.initialized();
            }));
        return Selection{selected.type,
                         parent_offset ? std::optional(*parent_offset + selected.offset) : std::nullopt,
                         std::move(path), Position{static_cast<std::uint64_t>(logical_index)},
                         selected.bit_width, selected.bit_offset};
    }

    // Whether each step of a destination path selects a union member.
    std::vector<bool> union_steps(const Path& path) const {
        std::vector<bool> result;
        result.reserve(path.size());
        auto type = root_;
        for (const auto& position : path) {
            const auto& current = module_.type(type);
            if (current.kind == hir::Type::Kind::Array) {
                result.push_back(false);
                type = *current.element;
            } else {
                const auto& record = module_.record(*current.record);
                result.push_back(record.is_union);
                type = record.members[*concrete(position)].type;
            }
        }
        return result;
    }

    // An entry may not initialize an object, or part of one, that an
    // earlier entry of its list initialized, except a part of an aggregate
    // that an earlier entry initialized from a value: a destination covers
    // its subobjects, and one member of a union covers the others. A list at
    // `depth` path steps holds the destinations of its entries.
    bool overlaps(Destinations& destinations, std::size_t depth, const Path& path,
                  SourceLocation location, bool whole, std::uint32_t& node) {
        const auto result = destinations.add(path, union_steps(path), depth, whole, node);
        if (result == Destinations::Result::Added) return false;
        error(location, result == Destinations::Result::Duplicate
            ? "duplicate destination in aggregate initializer"
            : "aggregate initializer entry overlaps the destination of an earlier entry");
        return true;
    }

    // The bytes of a scalar's value: a requested alignment or the f80
    // format pads its storage.
    std::uint64_t value_size(hir::TypeId type) const {
        const auto& current = module_.type(type);
        if (current.kind == hir::Type::Kind::Builtin && current.builtin == BuiltinType::F80) return 10;
        return hir::natural_size(module_, type, target_).value_or(0);
    }

    // Whether every byte of an object of `type` belongs to its value.
    bool dense(hir::TypeId type) const {
        const auto& current = module_.type(type);
        if (current.kind == hir::Type::Kind::Array) return current.element && dense(*current.element);
        const auto size = hir::layout_size(module_, type, target_);
        if (!size) return false;
        if (current.kind != hir::Type::Kind::Record) return value_size(type) == *size;
        const auto& record = module_.record(*current.record);
        if (record.is_union) return false;
        std::uint64_t end = 0;
        for (const auto& member : record.members) {
            const auto member_size = hir::layout_size(module_, member.type, target_);
            if (member.bit_width || !member.initialized() || member.offset != end || !member_size ||
                !dense(member.type)) return false;
            end += *member_size;
        }
        return end == *size;
    }

    void bit_field_region(const hir::RecordMember& member, std::uint64_t offset,
                          std::vector<InitializerValueRegion>& regions) const {
        const auto mask = shift_left(mask_to(bit_not(UInt128{}), *member.bit_width), member.bit_offset);
        regions.push_back({offset + member.offset, value_size(member.type), mask, 1, 0});
    }

    // The regions of `count` elements of `type`, `stride` bytes apart from
    // `offset` on.
    void element_regions(hir::TypeId type, std::uint64_t offset, std::uint64_t count, std::uint64_t stride,
                         std::vector<InitializerValueRegion>& regions) const {
        if (count == 0) return;
        if (dense(type)) {
            regions.push_back({offset, stride * count, {}, 1, 0});
            return;
        }
        std::vector<InitializerValueRegion> element;
        whole_regions(type, 0, element);
        merge_regions(element);
        const bool flat = std::all_of(element.begin(), element.end(),
            [](const InitializerValueRegion& region) { return region.count == 1; });
        for (std::uint64_t index = 0; index < (flat ? 1 : count); ++index)
            for (auto region : element) {
                region.offset += offset + index * stride;
                if (flat) {
                    region.count = count;
                    region.stride = stride;
                }
                regions.push_back(region);
            }
    }

    // Every part of an object of `type`, `offset` bytes into a value,
    // without its padding.
    void whole_regions(hir::TypeId type, std::uint64_t offset,
                       std::vector<InitializerValueRegion>& regions) const {
        const auto& current = module_.type(type);
        if (current.kind == hir::Type::Kind::Array && current.element) {
            element_regions(*current.element, offset, current.lanes,
                            hir::layout_size(module_, *current.element, target_).value_or(0), regions);
            return;
        }
        if (current.kind == hir::Type::Kind::Record && current.record) {
            const auto& record = module_.record(*current.record);
            if (record.is_union) {
                // A union takes the representation of its largest member.
                std::uint64_t size = 0;
                for (const auto& member : record.members)
                    size = std::max(size, hir::layout_size(module_, member.type, target_).value_or(0));
                regions.push_back({offset, size, {}, 1, 0});
                return;
            }
            for (const auto& member : record.members) {
                if (!member.initialized()) continue;
                if (member.bit_width) bit_field_region(member, offset, regions);
                else whole_regions(member.type, offset + member.offset, regions);
            }
            return;
        }
        regions.push_back({offset, value_size(type), {}, 1, 0});
    }

    // The parts of the object of `node`, of `type` and `offset` bytes into an
    // aggregate value, that the value supplies: all but those that later
    // entries of the list replace, and nothing of a union they enter.
    void value_regions(const Destinations& destinations, std::uint32_t node, hir::TypeId type,
                       std::uint64_t offset, std::vector<InitializerValueRegion>& regions) const {
        if (destinations.childless(node)) {
            whole_regions(type, offset, regions);
            return;
        }
        const auto& current = module_.type(type);
        if (current.kind == hir::Type::Kind::Array && current.element) {
            const auto stride = hir::layout_size(module_, *current.element, target_).value_or(0);
            std::uint64_t next = 0;
            for (const auto& [index, child] : destinations.children(node)) {
                if (index >= current.lanes) break;
                element_regions(*current.element, offset + next * stride, index - next, stride, regions);
                if (!destinations.terminal(child))
                    value_regions(destinations, child, *current.element, offset + index * stride, regions);
                next = index + 1;
            }
            element_regions(*current.element, offset + next * stride, current.lanes - next, stride, regions);
            return;
        }
        const auto& record = module_.record(*current.record);
        if (record.is_union) return;
        for (std::size_t index = 0; index < record.members.size(); ++index) {
            const auto& member = record.members[index];
            if (!member.initialized()) continue;
            const auto child = destinations.child(node, Position{std::uint64_t{index}});
            if (member.bit_width) {
                if (!child) bit_field_region(member, offset, regions);
            } else if (!child) {
                whole_regions(member.type, offset + member.offset, regions);
            } else if (!destinations.terminal(*child)) {
                value_regions(destinations, *child, member.type, offset + member.offset, regions);
            }
        }
    }

    // Orders the regions, joins adjacent bytes and the masks of one unit.
    static void merge_regions(std::vector<InitializerValueRegion>& regions) {
        std::stable_sort(regions.begin(), regions.end(), [](const auto& left, const auto& right) {
            return left.offset != right.offset ? left.offset < right.offset
                 : left.mask.has_value() < right.mask.has_value();
        });
        std::vector<InitializerValueRegion> merged;
        for (auto& region : regions) {
            if (region.size == 0 || region.count == 0) continue;
            if (region.mask && *region.mask == mask_to(bit_not(UInt128{}), static_cast<unsigned>(region.size * 8)))
                region.mask.reset();
            if (!merged.empty() && region.count == 1 && merged.back().count == 1) {
                auto& last = merged.back();
                if (!region.mask && !last.mask && last.offset + last.size == region.offset) {
                    last.size += region.size;
                    continue;
                }
                if (region.mask && last.mask && last.offset == region.offset && last.size == region.size) {
                    *last.mask = bit_or(*last.mask, *region.mask);
                    if (*last.mask == mask_to(bit_not(UInt128{}), static_cast<unsigned>(last.size * 8)))
                        last.mask.reset();
                    continue;
                }
            }
            merged.push_back(region);
        }
        regions = std::move(merged);
    }

    // Whether `type` is an array of u8, which a string initializes.
    bool byte_array(hir::TypeId type) const {
        const auto& current = module_.type(type);
        return current.kind == hir::Type::Kind::Array && current.element &&
               module_.type(*current.element).kind == hir::Type::Kind::Builtin &&
               module_.type(*current.element).builtin == BuiltinType::U8;
    }

    // An entry that initializes an aggregate from a value. Unless it is a
    // u8 array's string, its regions follow from the later entries of its list.
    void whole(const Expr& value, const Selection& selection, std::uint32_t node,
               std::vector<std::pair<std::size_t, std::uint32_t>>& wholes) {
        if (types_only_ || !deferred_indices_.empty())
            result_.type_items.emplace_back(&value, selection.type);
        if (!selection.offset) return;
        const auto natural = hir::layout_alignment(module_, selection.type, target_).value_or(1);
        const bool string = value.kind == Expr::Kind::String && byte_array(selection.type);
        result_.items.push_back({&value, selection.type, *selection.offset,
            offset_alignment(static_cast<unsigned>(natural), *selection.offset),
            std::nullopt, 0, !string, {}});
        if (!string) wholes.emplace_back(result_.items.size() - 1, node);
    }

    void list(const Expr& source, hir::TypeId type, std::optional<std::uint64_t> offset,
              Path path) {
        if (!aggregate(module_, type)) {
            error(source.location,
                               "brace initializer requires an aggregate or scalar destination");
            result_.valid = false;
            return;
        }
        const auto& aggregate_type = module_.type(type);
        const bool is_union = aggregate_type.kind == hir::Type::Kind::Record &&
                              aggregate_type.record &&
                              module_.record(*aggregate_type.record).is_union;
        // An exhaustive record's list must name each member directly.
        const hir::Record* exhaustive{};
        if (aggregate_type.kind == hir::Type::Kind::Record && aggregate_type.record && !is_union) {
            const auto& record = module_.record(*aggregate_type.record);
            if (record.definition && record.definition->attribute("exhaustive")) exhaustive = &record;
        }
        std::vector<bool> named(exhaustive ? exhaustive->members.size() : 0);
        Destinations destinations;
        Position cursor{std::uint64_t{0}};
        bool union_entry{}, union_whole{}, based{};
        std::vector<std::pair<std::size_t, std::uint32_t>> wholes;
        for (const auto& entry : source.initializer_entries) {
            if (!entry.value) {
                result_.valid = false;
                continue;
            }
            // The base: the first entry, a value of the list's own type or a
            // u8 array's string, initializes the whole destination.
            if (&entry == &source.initializer_entries.front() && entry.designators.empty() &&
                (entry.base || (entry.value->kind == Expr::Kind::String && byte_array(type)))) {
                if (dynamic_outer_ && path.empty() && !types_only_) {
                    error(entry.location, "a variable-length array cannot take its initial value from an expression");
                    continue;
                }
                destinations.base();
                based = union_whole = union_entry = true;
                whole(*entry.value, {type, offset, path, Position{std::uint64_t{0}}, std::nullopt, 0}, 0, wholes);
                continue;
            }
            if (based && entry.designators.empty()) {
                error(entry.location, "excess entry in aggregate initializer");
                continue;
            }
            if (is_union && union_entry && !union_whole) {
                error(entry.location,
                                   "excess entry in union initializer");
                result_.valid = false;
                continue;
            }
            std::optional<Selection> selection;
            if (entry.designators.empty()) {
                selection = child(type, offset, path, cursor,
                                  entry.location);
            } else {
                selection = designate(type, offset, path,
                                      entry.designators.front());
            }
            if (!selection) continue;
            if (exhaustive)
                if (const auto index = concrete(selection->path[path.size()])) named[*index] = true;
            cursor = is_union ? Position{std::uint64_t{1}} : next_position(selection->direct_index);
            const bool first_union_entry = !union_entry;
            union_entry = true;
            for (std::size_t index = 1;
                 index < entry.designators.size() && selection; ++index) {
                selection = designate(selection->type, selection->offset,
                                      std::move(selection->path),
                                      entry.designators[index]);
            }
            if (!selection) continue;
            const auto* value = entry.value.get();
            if (value->kind == Expr::Kind::AggregateInitializer) {
                value = unbraced(*value, selection->type);
                if (!value) continue;
            }
            const bool nested = value == entry.value.get() && value->kind == Expr::Kind::AggregateInitializer;
            const bool aggregate_value = !nested && aggregate(module_, selection->type);
            if (first_union_entry) union_whole = aggregate_value;
            std::uint32_t node{};
            if (overlaps(destinations, path.size(), selection->path, entry.location, aggregate_value, node))
                continue;
            // A nested list initializes its whole destination, so its own
            // entries need only be distinct from each other.
            if (nested) list(*value, selection->type, selection->offset, selection->path);
            else if (aggregate_value) whole(*value, *selection, node, wholes);
            else item(*value, *selection);
        }
        if (!types_only_ && deferred_indices_.empty())
            for (const auto& [index, node] : wholes) {
                auto& copy = result_.items[index];
                value_regions(destinations, node, copy.type, 0, copy.regions);
                merge_regions(copy.regions);
            }
        // A base gives every member a value.
        for (std::size_t index = 0; index < named.size() && !based; ++index) {
            const auto& member = exhaustive->members[index];
            if (named[index] || !member.initialized()) continue;
            error(source.location, "initializer of exhaustive record '" + exhaustive->source_name +
                (member.name.empty() ? "' does not initialize its anonymous embedded member"
                                     : "' does not name member '" + member.name + "'"));
            break;
        }
    }

    const hir::Module& module_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    Plan result_;
    hir::TypeId root_;
    bool dynamic_outer_{};
    std::span<const Expr* const> deferred_indices_;
    bool types_only_{};
    std::span<const hir::TypeId> pending_extents_;
};

struct TypeHash {
    std::size_t operator()(hir::TypeId id) const { return std::hash<std::uint32_t>{}(id.value); }
};
using SourceTypes = std::unordered_map<hir::TypeId, TypePtr, TypeHash>;

SourceTypes source_types_for(const TypePtr& destination, const Program& program, hir::Module& layout,
                            bool shapes_only = false) {
    SourceTypes result;
    const auto visit = [&](const auto& self, const TypePtr& type) -> void {
        if (!type) return;
        const auto id = layout.intern_type(type);
        if (!result.emplace(id, type).second) return;
        self(self, type->element);
        if (type->kind != Type::Kind::Record) return;
        if (const auto record = program.record_definition(type->nominal_key())) {
            const auto record_id = *layout.type(id).record;
            std::vector<hir::RecordMember> members;
            for (const auto& member : record->members) {
                self(self, member.type);
                if (shapes_only) {
                    hir::RecordMember shape;
                    shape.location = member.location;
                    shape.name = member.name;
                    shape.fresh = member.fresh;
                    shape.embedded = member.embedded;
                    shape.type = layout.intern_type(member.type);
                    members.push_back(std::move(shape));
                }
            }
            if (shapes_only) {
                auto& shape = layout.records[record_id.value];
                shape.members = std::move(members);
                shape.is_union = record->is_union;
                shape.complete = false; // Shape is never a placeholder physical layout.
                shape.alignment_complete = false;
                shape.retained_definition = record;
                shape.definition = record.get();
                layout.promote_members(record_id);
            }
        }
    };
    visit(visit, destination);
    return result;
}

} // namespace

UnbracedInitializer unbrace(const Expr& source, const hir::Module& module, hir::TypeId type) {
    const auto& destination = module.type(type);
    const bool scalar = destination.kind == hir::Type::Kind::Pointer ||
        (destination.kind == hir::Type::Kind::Builtin && destination.builtin != BuiltinType::Void);
    const bool byte_array = destination.kind == hir::Type::Kind::Array && destination.element &&
        module.type(*destination.element).kind == hir::Type::Kind::Builtin &&
        module.type(*destination.element).builtin == BuiltinType::U8;
    return unbrace_initializer(source, scalar, byte_array);
}

Plan build(const Expr& source, hir::TypeId type, const hir::Module& module,
           const TargetInfo& target, Diagnostics& diagnostics) {
    return Planner(module, target, diagnostics).run(source, type);
}

Plan build_dynamic_array(const Expr& source, hir::TypeId type,
                         const hir::Module& module,
                         const TargetInfo& target,
                         Diagnostics& diagnostics) {
    return Planner(module, target, diagnostics, true).run(source, type);
}

EvaluationInitializerPlan build_for_evaluation(
    const Expr& expression, const TypePtr& destination, const Program& program,
    hir::Module& layout, const TargetInfo& target) {
    const auto source_types = source_types_for(destination, program, layout);
    std::ostringstream output;
    Diagnostics quiet(output);
    const auto type = layout.intern_type(destination);
    const bool dynamic_array = destination && destination->kind == Type::Kind::Array &&
        destination->lanes == 0;
    const auto plan = dynamic_array
        ? build_dynamic_array(expression, type, layout, target, quiet)
        : build(expression, type, layout, target, quiet);
    EvaluationInitializerPlan result;
    result.minimum_elements = plan.minimum_elements;
    result.valid = plan.valid;
    result.error_location = plan.error_location;
    result.error_message = plan.error_message;
    for (const auto& item : plan.items) {
        const auto found = source_types.find(item.type);
        if (found == source_types.end()) { result.valid = false; break; }
        result.items.push_back({item.expression, found->second,
            {item.offset, item.alignment, item.bit_width, item.bit_offset}, item.whole, item.regions});
    }
    return result;
}

EvaluationInitializerTypePlan types_for_evaluation(
    const Expr& expression, const TypePtr& destination, const Program& program,
    hir::Module& layout, const TargetInfo& target, std::span<const Expr* const> deferred_indices) {
    // Work in an isolated registry: replacing physical members by source shape
    // must not mutate a cached layout used by execution or emission. Only the
    // destination's types are interned, so nothing else of the layout is copied.
    hir::Module shapes;
    shapes.address_bits = layout.address_bits;
    shapes.default_abi = layout.default_abi;
    shapes.abi_names = layout.abi_names;
    const auto source_types = source_types_for(destination, program, shapes, true);
    std::vector<hir::TypeId> pending_extents;
    for (const auto& [id, source] : source_types)
        if (source->kind == Type::Kind::Array && !source->lanes &&
            (source->array_bound || source->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext))
            pending_extents.push_back(id);
    std::ostringstream output;
    Diagnostics quiet(output);
    const bool dynamic = destination && destination->kind == Type::Kind::Array && destination->lanes == 0;
    const auto plan = Planner(shapes, target, quiet, dynamic, deferred_indices, true, pending_extents)
        .run(expression, shapes.intern_type(destination));
    EvaluationInitializerTypePlan result;
    result.minimum_elements = plan.minimum_elements;
    result.outer_extent_deferred = plan.outer_extent_deferred;
    result.valid = plan.valid;
    result.error_location = plan.error_location;
    result.error_message = plan.error_message;
    for (const auto& [value, type] : plan.type_items) {
        const auto found = source_types.find(type);
        if (found == source_types.end()) { result.valid = false; break; }
        result.items.emplace_back(value, found->second);
    }
    return result;
}

} // namespace cross::initializer
