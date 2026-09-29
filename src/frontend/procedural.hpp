// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "frontend/semantic.hpp"

#include <string_view>

namespace cross {

struct ExpansionFunctionSource {
    FunctionDecl function;
    bool syntax_expander{};
};
struct ExpansionFunctionHead {
    std::size_t after_specifiers{};
    std::size_t name_index{};
    std::size_t body_index{};
    SourceLocation location;
    bool syntax_expander{};
    bool static_storage{};
    bool global_storage{};
    bool result_location{};
    std::string error;
    SourceLocation error_location;
};
// Recognize a translation-time function's role in its declaration attributes,
// including those after its return type or parameter declarator.
std::optional<ExpansionFunctionHead> expansion_function_head(
    const std::vector<Token>& tokens, std::size_t index);
// Parse one original declaration selected by the item parser, never scan raw
// captured inputs for declarations. Advances index past the bounded source.
std::optional<ExpansionFunctionSource> parse_expansion_function(
    const std::vector<Token>& tokens, std::size_t& index, Diagnostics& diagnostics,
    unsigned address_bits, std::shared_ptr<const SyntaxContext> definition_context = {});

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
