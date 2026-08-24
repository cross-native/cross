// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"

#include <string_view>

namespace cross {

// Executes explicit token macro invocations after ordinary preprocessing and
// before the resulting token region is parsed as Cross.
const SourceFile* expand_procedural_macros(SourceManager& sources,
                                           const std::filesystem::path& path,
                                           std::string_view source,
                                           Diagnostics& diagnostics);

} // namespace cross
