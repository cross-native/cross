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
#include <string>
#include <utility>
#include <vector>

namespace cross::initializer {
namespace {

using Path = std::vector<std::uint32_t>;

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
            Diagnostics& diagnostics)
        : module_(module), target_(target), diagnostics_(diagnostics) {}

    Plan run(const Expr& source, hir::TypeId type) {
        if (source.kind != Expr::Kind::AggregateInitializer) {
            diagnostics_.error(source.location,
                               "aggregate initializer requires a brace list");
            result_.valid = false;
            return std::move(result_);
        }
        list(source, type, 0, {});
        return std::move(result_);
    }

private:
    struct Selection {
        hir::TypeId type;
        std::uint64_t offset{};
        Path path;
        std::size_t direct_index{};
    };

    std::optional<std::uint64_t> index_value(const Expr& expression) {
        const Expr* source = &expression;
        while (source->kind == Expr::Kind::Parenthesized && source->left) {
            source = source->left.get();
        }
        if (!source->evaluated_integer ||
            source->evaluated_integer->value.high != 0) {
            diagnostics_.error(
                expression.location,
                "array initializer designator requires a nonnegative integer constant");
            result_.valid = false;
            return std::nullopt;
        }
        return source->evaluated_integer->value.low;
    }

    std::optional<Selection> child(hir::TypeId parent,
                                   std::uint64_t parent_offset,
                                   Path path, std::size_t index,
                                   SourceLocation location) {
        const auto& type = module_.type(parent);
        if (type.kind == hir::Type::Kind::Array && type.element) {
            if (type.lanes == 0 || index >= type.lanes) {
                diagnostics_.error(location,
                                   "array initializer designator is out of range");
                result_.valid = false;
                return std::nullopt;
            }
            const auto size = hir::layout_size(module_, *type.element, target_);
            if (!size || index >
                             std::numeric_limits<std::uint64_t>::max() /
                                 *size) {
                diagnostics_.error(location,
                                   "array initializer offset overflows target storage");
                result_.valid = false;
                return std::nullopt;
            }
            path.push_back(static_cast<std::uint32_t>(index));
            return Selection{*type.element,
                             parent_offset + index * *size,
                             std::move(path), index};
        }
        if (type.kind == hir::Type::Kind::Record && type.record) {
            const auto& record = module_.record(*type.record);
            if (index >= record.members.size() ||
                (record.is_union && index != 0)) {
                diagnostics_.error(location,
                                   "excess entry in aggregate initializer");
                result_.valid = false;
                return std::nullopt;
            }
            const auto& member = record.members[index];
            path.push_back(static_cast<std::uint32_t>(index));
            return Selection{member.type, parent_offset + member.offset,
                             std::move(path), index};
        }
        diagnostics_.error(location,
                           "initializer designator requires an aggregate destination");
        result_.valid = false;
        return std::nullopt;
    }

    std::optional<Selection> designate(
        hir::TypeId parent, std::uint64_t parent_offset, Path path,
        const Expr::InitializerDesignator& designator) {
        const auto& type = module_.type(parent);
        if (designator.kind == Expr::InitializerDesignator::Kind::Index) {
            if (type.kind != hir::Type::Kind::Array || !designator.index) {
                diagnostics_.error(
                    designator.location,
                    "array designator requires an array destination");
                result_.valid = false;
                return std::nullopt;
            }
            const auto index = index_value(*designator.index);
            if (!index || *index > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            return child(parent, parent_offset, std::move(path),
                         static_cast<std::size_t>(*index),
                         designator.location);
        }
        if (type.kind != hir::Type::Kind::Record || !type.record) {
            diagnostics_.error(designator.location,
                               "member designator requires a record destination");
            result_.valid = false;
            return std::nullopt;
        }
        const auto& record = module_.record(*type.record);
        const auto member = std::find_if(
            record.members.begin(), record.members.end(),
            [&](const hir::RecordMember& candidate) {
                return candidate.name == designator.member;
            });
        if (member == record.members.end()) {
            diagnostics_.error(
                designator.location,
                "record initializer has no member named '" +
                    designator.member + "'");
            result_.valid = false;
            return std::nullopt;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(record.members.begin(), member));
        path.push_back(static_cast<std::uint32_t>(index));
        return Selection{member->type, parent_offset + member->offset,
                         std::move(path), index};
    }

    bool occupied(const Path& path, SourceLocation location) {
        if (std::any_of(paths_.begin(), paths_.end(), [&](const Path& prior) {
                return prefix(path, prior) || prefix(prior, path);
            })) {
            diagnostics_.error(location,
                               "duplicate destination in aggregate initializer");
            result_.valid = false;
            return true;
        }
        paths_.push_back(path);
        return false;
    }

    void list(const Expr& source, hir::TypeId type, std::uint64_t offset,
              Path path) {
        if (!aggregate(module_, type)) {
            diagnostics_.error(source.location,
                               "brace initializer requires an aggregate destination");
            result_.valid = false;
            return;
        }
        const auto& aggregate_type = module_.type(type);
        const bool is_union = aggregate_type.kind == hir::Type::Kind::Record &&
                              aggregate_type.record &&
                              module_.record(*aggregate_type.record).is_union;
        std::size_t cursor{};
        bool union_entry{};
        for (const auto& entry : source.initializer_entries) {
            if (!entry.value) {
                result_.valid = false;
                continue;
            }
            if (is_union && union_entry) {
                diagnostics_.error(entry.location,
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
            cursor = is_union ? 1 : selection->direct_index + 1;
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
            const auto natural_value =
                hir::layout_alignment(module_, selection->type, target_)
                    .value_or(1);
            const auto natural = natural_value <=
                                         std::numeric_limits<unsigned>::max()
                                     ? static_cast<unsigned>(natural_value)
                                     : 1U;
            result_.items.push_back(
                {entry.value.get(), selection->type, selection->offset,
                 offset_alignment(natural, selection->offset)});
        }
    }

    const hir::Module& module_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    Plan result_;
    std::vector<Path> paths_;
};

} // namespace

Plan build(const Expr& source, hir::TypeId type, const hir::Module& module,
           const TargetInfo& target, Diagnostics& diagnostics) {
    return Planner(module, target, diagnostics).run(source, type);
}

} // namespace cross::initializer
