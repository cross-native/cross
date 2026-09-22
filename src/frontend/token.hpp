// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace cross {

enum class TokenKind {
    End, Identifier, BuiltinName, Integer, Floating, String, Character,
    Punctuator, Invalid,
};

struct Token {
    TokenKind kind{TokenKind::Invalid};
    std::string_view text;
    SourceLocation location;

    [[nodiscard]] bool is(std::string_view spelling) const { return text == spelling; }
};

// Translation-time token values own spelling but retain structured provenance.
// Copying a token value never reparses it or changes its lexical identity.
struct MetaToken {
    TokenKind kind{TokenKind::Invalid};
    std::string text;
    TokenOrigin origin;

    MetaToken() = default;
    explicit MetaToken(const Token& token)
        : kind(token.kind), text(token.text), origin(token_origin(token.location)) {}
};

using TokenSequence = std::vector<MetaToken>;

} // namespace cross
