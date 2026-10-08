// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/token_tree.hpp"

namespace cross {
namespace {

struct DelimiterToken {
    TokenDelimiter delimiter{TokenDelimiter::None};
    bool opening{};
};

DelimiterToken delimiter_token(const MetaToken& token) {
    if (token.kind != TokenKind::Punctuator) return {};
    if (token.text == "(") return {TokenDelimiter::Parenthesis, true};
    if (token.text == ")") return {TokenDelimiter::Parenthesis, false};
    if (token.text == "[") return {TokenDelimiter::Bracket, true};
    if (token.text == "]") return {TokenDelimiter::Bracket, false};
    if (token.text == "{") return {TokenDelimiter::Brace, true};
    if (token.text == "}") return {TokenDelimiter::Brace, false};
    if (token.text == "[[") return {TokenDelimiter::Attribute, true};
    if (token.text == "]]") return {TokenDelimiter::Attribute, false};
    return {};
}

} // namespace

TokenTreeScanError scan_token_trees(std::span<const MetaToken> tokens,
    std::uint64_t maximum_depth, TokenTreeBalance balance, const TokenTreeScanHooks& hooks) {
    std::vector<TokenDelimiter> delimiters;
    std::size_t retained_depth{};
    TokenTreeRange element;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        if (hooks.visit_token && !hooks.visit_token(index)) return TokenTreeScanError::Cancelled;
        const auto token = delimiter_token(tokens[index]);
        if (delimiters.empty())
            element = {index, index + 1, token.opening ? token.delimiter : TokenDelimiter::None};
        if (token.delimiter != TokenDelimiter::None) {
            if (token.opening) {
                if (delimiters.size() >= maximum_depth) return TokenTreeScanError::DepthLimit;
                if (delimiters.size() == retained_depth) {
                    if (hooks.retain_depth && !hooks.retain_depth()) return TokenTreeScanError::Cancelled;
                    ++retained_depth;
                }
                delimiters.push_back(token.delimiter);
            } else {
                if (balance == TokenTreeBalance::Required &&
                    (delimiters.empty() || delimiters.back() != token.delimiter))
                    return TokenTreeScanError::UnmatchedDelimiter;
                if (!delimiters.empty()) delimiters.pop_back();
            }
        }
        if (delimiters.empty()) {
            element.end = index + 1;
            if (hooks.complete && !hooks.complete(element)) return TokenTreeScanError::Cancelled;
        }
    }
    return balance == TokenTreeBalance::Required && !delimiters.empty()
        ? TokenTreeScanError::UnterminatedGroup : TokenTreeScanError::None;
}

} // namespace cross
