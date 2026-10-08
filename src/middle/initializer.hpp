// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/hir.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace cross {

class Diagnostics;
struct Expr;
struct TargetInfo;

namespace initializer {

struct Item {
    const Expr* expression{};
    hir::TypeId type;
    std::uint64_t offset{};
    unsigned alignment{1};
    std::optional<unsigned> bit_width;
    unsigned bit_offset{};
};

struct Plan {
    std::vector<Item> items;
    // Source validation can select types through unresolved array positions.
    // These entries deliberately carry no physical offset or alignment.
    std::vector<std::pair<const Expr*, hir::TypeId>> type_items;
    // Runtime-sized outer arrays require at least this many elements for all
    // explicit positional/designated destinations to exist.
    std::uint64_t minimum_elements{};
    // A symbolic index in the dynamic outer array makes its exact minimum
    // unknown. Symbolic indices in fixed nested arrays do not affect it.
    bool outer_extent_deferred{};
    bool valid{true};
    SourceLocation error_location;
    std::string error_message;
};

// Resolves successive and designated aggregate entries into typed byte-offset
// destinations. Missing storage is deliberately absent from the plan: static
// and automatic consumers zero the complete aggregate before applying items.
[[nodiscard]] Plan build(const Expr& initializer, hir::TypeId type,
                         const hir::Module& module,
                         const TargetInfo& target,
                         Diagnostics& diagnostics);

// Resolves one runtime-sized outer array. Nested aggregate dimensions remain
// fixed, and minimum_elements records the runtime extent required by the
// furthest explicit destination.
[[nodiscard]] Plan build_dynamic_array(const Expr& initializer,
                                       hir::TypeId type,
                                       const hir::Module& module,
                                       const TargetInfo& target,
                                       Diagnostics& diagnostics);

// Projects the same physical plan to source types for translation-time
// validation/execution. The caller supplies a target-resolved layout containing
// the destination's concrete records; no frontend layout defaults are used.
[[nodiscard]] EvaluationInitializerPlan build_for_evaluation(
    const Expr& initializer, const TypePtr& destination, const Program& program,
    hir::Module& layout, const TargetInfo& target);

// Uses source member shape and known extents without requesting byte offsets,
// record alignment or bit-field widths. The resolved registry preserves type
// and callable-ABI identity; execution still requires a complete physical plan.
[[nodiscard]] EvaluationInitializerTypePlan types_for_evaluation(
    const Expr& initializer, const TypePtr& destination, const Program& program,
    hir::Module& layout, const TargetInfo& target,
    std::span<const Expr* const> deferred_indices);

} // namespace initializer
} // namespace cross
