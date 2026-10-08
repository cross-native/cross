// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/token.hpp"

#include <functional>
#include <span>

namespace cross {

enum class TokenDelimiter { None, Parenthesis, Bracket, Brace, Attribute };
enum class TokenTreeBalance { Required, Fragments };
enum class TokenTreeScanError { None, UnmatchedDelimiter, UnterminatedGroup, DepthLimit, Cancelled };

// Half-open indexes into the original sequence. A structured splice is one
// opaque element, never a flattened sequence. Group contents are [begin+1,end-1).
// Only Required scans guarantee that these are complete balanced elements.
struct TokenTreeRange {
    std::size_t begin{};
    std::size_t end{};
    TokenDelimiter delimiter{TokenDelimiter::None};
    bool operator==(const TokenTreeRange&) const = default;
};

struct TokenTreeScanHooks {
    // Called once before inspecting each token. Returning false cancels the scan.
    std::function<bool(std::size_t)> visit_token;
    // Called once for each new stack high-water slot, before retaining that slot.
    // The caller owns work/storage policy; the scanner knows no host/target ABI.
    std::function<bool()> retain_depth;
    std::function<bool(TokenTreeRange)> complete;
};

// Read-only, iterative traversal. Fragments mode supports explicitly projected
// delimiters awaiting composition, but still enforces depth and hook failures.
// No callback reparses or expands a splice; provenance remains in the input.
TokenTreeScanError scan_token_trees(std::span<const MetaToken> tokens,
    std::uint64_t maximum_depth, TokenTreeBalance balance, const TokenTreeScanHooks& hooks);

} // namespace cross
