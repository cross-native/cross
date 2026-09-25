// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "frontend/semantic.hpp"

#include <string_view>

namespace cross {

// Executes explicit token macro invocations after ordinary preprocessing and
// before the resulting token region is parsed as Cross.
const SourceFile* expand_procedural_macros(SourceManager& sources,
                                           const std::filesystem::path& path,
                                           std::string_view source,
                                           Diagnostics& diagnostics,
                                           unsigned address_bits,
                                           const LayoutQuery& size_of,
                                           const LayoutQuery& align_of,
                                           EvaluationLimits limits = {},
                                           EvaluationLayout layout = {});
const SourceFile* expand_procedural_macros(SourceManager& sources,
                                           const SourceFile& source,
                                           Diagnostics& diagnostics,
                                           unsigned address_bits,
                                           const LayoutQuery& size_of,
                                           const LayoutQuery& align_of,
                                           EvaluationLimits limits = {},
                                           EvaluationLayout layout = {});

} // namespace cross
