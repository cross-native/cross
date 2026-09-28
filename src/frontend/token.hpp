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

// A grammar may consume the two characters of a lexical >> separately.
// Preserve their common source identity so a complete projection can recover
// the original token, while either terminal remains inspectable on its own.
struct SplitTokenSource {
    TokenKind kind;
    std::string spelling;
};

struct Token {
    TokenKind kind{TokenKind::Invalid};
    std::string_view text;
    SourceLocation location;
    ValueBinding value_binding;
    std::shared_ptr<const SplitTokenSource> split_source;
    std::size_t split_offset{};

    Token() = default;
    Token(TokenKind kind, std::string_view text, SourceLocation location)
        : kind(kind), text(text), location(location) {}

    [[nodiscard]] bool is(std::string_view spelling) const { return text == spelling; }
};

// Translation-time token values own spelling but retain structured provenance.
// Copying a token value never reparses it or changes its lexical identity.
struct MetaToken {
    TokenKind kind{TokenKind::Invalid};
    std::string text;
    TokenOrigin origin;
    std::shared_ptr<const SplitTokenSource> split_source;
    std::size_t split_offset{};

    MetaToken() = default;
    explicit MetaToken(const Token& token)
        : kind(token.kind), text(token.text), origin(token_origin(token.location)),
          split_source(token.split_source), split_offset(token.split_offset) {
        if (token.value_binding.kind != ValueBinding::Kind::Unknown)
            origin.value_binding = token.value_binding;
    }
};

using TokenSequence = std::vector<MetaToken>;

// Logical resource accounting, independent of the host C++ object layout.
inline constexpr std::uint64_t meta_token_storage_bytes = 192;

} // namespace cross
