// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/hir.hpp"

#include <cstdint>
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
    // Runtime-sized outer arrays require at least this many elements for all
    // explicit positional/designated destinations to exist.
    std::uint64_t minimum_elements{};
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

} // namespace initializer
} // namespace cross
