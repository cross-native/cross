// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/raw_mir.hpp"

#include <span>
#include <string>

namespace cross::native {

// The -fstack-usage report in GCC's .su format: a line
// `FILE:LINE:COLUMN:NAME<TAB>BYTES<TAB>KIND` per emitted function, in
// emission order.
[[nodiscard]] std::string stack_usage_report(
    const hir::Module& module, const CompilerOptions& options,
    std::span<const mir::FunctionAssembly> functions);

} // namespace cross::native
