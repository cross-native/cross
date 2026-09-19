// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "frontend/ast.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace cross {

// The single registry used by semantic validation, source queries, and driver
// inspection. Attribute spellings are contextual and therefore omit "$::".
std::span<const std::string_view> core_attribute_names();
bool is_known_attribute(std::string_view name);

// Expands explicit generic instances and deterministic translation-time calls.
// Ordinary visible functions are evaluated opportunistically when requested;
// forced evaluation is independent of that option. On success no generic type,
// generic call, staging wrapper, or evaluation-only function remains.
bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls = true,
                      std::string_view mangling = "default",
                      std::string_view default_abi = "default");

using LayoutQuery =
    std::function<std::optional<std::uint64_t>(const TypePtr&)>;

// Static assertions are retained until target HIR has established nominal
// layouts.  The callbacks keep target layout ownership out of the frontend.
bool finalize_target_constants(Program& program, Diagnostics& diagnostics,
                               const LayoutQuery& size_of,
                               const LayoutQuery& align_of);

// Evaluates one required integer expression once target layout callbacks are
// available. HIR uses this for bit-field widths and static patch-sink indices
// because record layout owns the allocation policy and can resolve dependent
// records lazily.
std::optional<Expr::IntegerConstant> evaluate_target_integer_constant(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {});

// Uses the same required-constant evaluator for each `aligned` placement.
// The caller supplies the subject only for a precise argument-count error.
std::optional<unsigned> evaluate_alignment_attribute(
    Program& program, const Attribute& attribute, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view subject, std::string_view source_namespace = {});

} // namespace cross
