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

bool prefix(const Path& left, const Path& right) {
    return left.size() <= right.size() &&
           std::equal(left.begin(), left.end(), right.begin());
}

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
        if (source.kind != Expr::Kind::AggregateInitializer) {
            error(source.location,
                               "aggregate initializer requires a brace list");
            result_.valid = false;
            return std::move(result_);
        }
        list(source, type, types_only_ ? std::nullopt : std::optional<std::uint64_t>{0}, {});
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
                if (candidate->name.empty()) continue;
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
        const auto member = std::find_if(
            record.members.begin(), record.members.end(),
            [&](const hir::RecordMember& candidate) {
                return candidate.member_name() == designator.member_name();
            });
        if (member == record.members.end()) {
            error(
                designator.location,
                "record initializer has no member named '" +
                    designator.member + "'");
            result_.valid = false;
            return std::nullopt;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(record.members.begin(), member));
        path.push_back(static_cast<std::uint64_t>(index));
        const auto logical_index = static_cast<std::size_t>(std::count_if(
            record.members.begin(), member,
            [](const hir::RecordMember& candidate) {
                return !candidate.name.empty();
            }));
        return Selection{member->type,
                         parent_offset ? std::optional(*parent_offset + member->offset) : std::nullopt,
                         std::move(path), Position{static_cast<std::uint64_t>(logical_index)},
                         member->bit_width, member->bit_offset};
    }

    bool occupied(const Path& path, SourceLocation location) {
        if (std::any_of(paths_.begin(), paths_.end(), [&](const Path& prior) {
                return prefix(path, prior) || prefix(prior, path);
            })) {
            error(location,
                               "duplicate destination in aggregate initializer");
            result_.valid = false;
            return true;
        }
        paths_.push_back(path);
        return false;
    }

    void list(const Expr& source, hir::TypeId type, std::optional<std::uint64_t> offset,
              Path path) {
        if (!aggregate(module_, type)) {
            error(source.location,
                               "brace initializer requires an aggregate destination");
            result_.valid = false;
            return;
        }
        const auto& aggregate_type = module_.type(type);
        const bool is_union = aggregate_type.kind == hir::Type::Kind::Record &&
                              aggregate_type.record &&
                              module_.record(*aggregate_type.record).is_union;
        Position cursor{std::uint64_t{0}};
        bool union_entry{};
        for (const auto& entry : source.initializer_entries) {
            if (!entry.value) {
                result_.valid = false;
                continue;
            }
            if (is_union && union_entry) {
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
            cursor = is_union ? Position{std::uint64_t{1}} : next_position(selection->direct_index);
            union_entry = true;
            for (std::size_t index = 1;
                 index < entry.designators.size() && selection; ++index) {
                selection = designate(selection->type, selection->offset,
                                      std::move(selection->path),
                                      entry.designators[index]);
            }
            if (!selection) continue;
            if (entry.value->kind == Expr::Kind::AggregateInitializer) {
                list(*entry.value, selection->type, selection->offset,
                     std::move(selection->path));
                continue;
            }
            if (occupied(selection->path, entry.location)) continue;
            if (types_only_ || !deferred_indices_.empty())
                result_.type_items.emplace_back(entry.value.get(), selection->type);
            if (!selection->offset) continue;
            const auto natural_value =
                hir::layout_alignment(module_, selection->type, target_)
                    .value_or(1);
            const auto natural = natural_value <=
                                         std::numeric_limits<unsigned>::max()
                                     ? static_cast<unsigned>(natural_value)
                                     : 1U;
            result_.items.push_back(
                {entry.value.get(), selection->type, *selection->offset,
                 offset_alignment(natural, *selection->offset),
                 selection->bit_width, selection->bit_offset});
        }
    }

    const hir::Module& module_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    Plan result_;
    std::vector<Path> paths_;
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
            }
        }
    };
    visit(visit, destination);
    return result;
}

} // namespace

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
            {item.offset, item.alignment, item.bit_width, item.bit_offset}});
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
