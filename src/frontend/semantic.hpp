// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "frontend/ast.hpp"

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
                      std::string_view mangling = "default");

} // namespace cross
