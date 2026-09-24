// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "common/source.hpp"

#include <span>
#include <vector>

namespace cross {

struct EmbedDiscovery {
    const SourceFile* source{};
    std::vector<std::filesystem::path> dependencies;
};

// Runs after source-macro expansion and before procedural expansion. A normal
// cpp -E invocation preserves missing asset expressions; dependency output
// and integrated cc validate the selected regular/readable file.
EmbedDiscovery discover_embeds(SourceManager& sources, const SourceFile& source,
                               std::span<const SourceLocation> line_origins,
                               const CompilerOptions& options,
                               Diagnostics& diagnostics, bool validate);

// Reject newly constructed or modified embed spellings after token expansion.
bool validate_embeds(const SourceFile& source, Diagnostics& diagnostics);

// Until the byte evaluator and materializer are installed, stop before the
// ordinary parser reports an unrelated array-bound or unknown-call error.
bool diagnose_unimplemented_embed_values(const SourceFile& source,
                                          Diagnostics& diagnostics);

} // namespace cross
