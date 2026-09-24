// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "common/source.hpp"

#include <span>
#include <unordered_map>
#include <vector>

namespace cross {

struct EmbedDiscovery {
    const SourceFile* source{};
    std::vector<std::filesystem::path> dependencies;
};

// Runs after source-macro expansion and before procedural expansion. A normal
// cpp -E invocation preserves missing asset expressions; dependency output
// and integrated cc validate the selected regular/readable file.
using EmbedSnapshots = std::unordered_map<std::string, std::shared_ptr<EmbedSnapshot>>;

EmbedDiscovery discover_embeds(SourceManager& sources, const SourceFile& source,
                               std::span<const SourceLocation> line_origins,
                               const CompilerOptions& options,
                               Diagnostics& diagnostics, bool validate,
                               EmbedSnapshots* snapshots = nullptr);

// Reject newly constructed or modified embed spellings after token expansion.
bool validate_embeds(const SourceFile& source, Diagnostics& diagnostics);

} // namespace cross
