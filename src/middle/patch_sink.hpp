// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/mir.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace cross {

class Diagnostics;
struct Expr;
struct TargetInfo;

namespace mir {

using PatchSinkObjectResolver =
    std::function<const hir::Object*(std::string_view)>;

// Resolves the deliberately narrow static-sink grammar without lowering a
// general lvalue: one static object followed only by direct member and
// constant array selections.
[[nodiscard]] std::optional<PatchSink> resolve_patch_sink_designator(
    const Expr& expression, const hir::Module& module,
    const TargetInfo& target, const PatchSinkObjectResolver& resolve_object,
    Diagnostics& diagnostics);

} // namespace mir
} // namespace cross
