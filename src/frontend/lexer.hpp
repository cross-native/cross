// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "frontend/token.hpp"

#include <string_view>
#include <vector>

namespace cross {

class Lexer {
public:
    Lexer(const SourceFile& source, Diagnostics& diagnostics);
    std::vector<Token> lex();

private:
    [[nodiscard]] char peek(std::size_t lookahead = 0) const;
    char take();
    void skip_trivia();
    Token token(TokenKind kind, std::size_t start, SourceLocation location) const;
    Token lex_identifier();
    Token lex_builtin();
    Token lex_number();
    Token lex_quoted(char quote, TokenKind kind);
    Token lex_punctuator();

    const SourceFile& source_;
    Diagnostics& diagnostics_;
    std::size_t offset_{};
    unsigned line_{1};
    unsigned column_{1};
};

} // namespace cross
