// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"

#include <cctype>
#include <string>

namespace cross {
namespace {

bool ident_start(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

bool ident_continue(char ch) {
    return ident_start(ch) || (ch >= '0' && ch <= '9');
}

} // namespace

Lexer::Lexer(const SourceFile& source, Diagnostics& diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

char Lexer::peek(std::size_t lookahead) const {
    const auto position = offset_ + lookahead;
    return position < source_.text.size() ? source_.text[position] : '\0';
}

char Lexer::take() {
    const char ch = peek();
    if (ch == '\0') return ch;
    ++offset_;
    if (ch == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return ch;
}

Token Lexer::token(TokenKind kind, std::size_t start, SourceLocation location) const {
    Token result(kind, std::string_view(source_.text).substr(start, offset_ - start), location);
    result.value_binding = token_origin(location).value_binding;
    if (const auto* origin = source_.source_token_origin_at(start);
        origin && origin->begin == start && origin->end == offset_ && origin->splice) {
        result.kind = TokenKind::StructuredSplice;
        result.splice = origin->splice;
    }
    return result;
}

void Lexer::skip_trivia() {
    for (;;) {
        while (std::isspace(static_cast<unsigned char>(peek())) != 0) take();
        if (peek() == '/' && peek(1) == '/') {
            while (peek() != '\0' && take() != '\n') {}
            continue;
        }
        if (peek() == '/' && peek(1) == '*') {
            const SourceLocation location{&source_, offset_, line_, column_};
            take(); take();
            while (peek() != '\0' && !(peek() == '*' && peek(1) == '/')) take();
            if (peek() == '\0') {
                diagnostics_.error(location, "unterminated block comment");
                return;
            }
            take(); take();
            continue;
        }
        break;
    }
}

Token Lexer::lex_identifier() {
    const auto start = offset_;
    const SourceLocation location{&source_, offset_, line_, column_};
    take();
    while (ident_continue(peek())) take();
    return token(TokenKind::Identifier, start, location);
}

Token Lexer::lex_builtin() {
    const auto start = offset_;
    const SourceLocation location{&source_, offset_, line_, column_};
    take();
    if (peek() != ':' || peek(1) != ':') {
        diagnostics_.error(location, "'$' is valid only as the '$::' built-in root");
        return token(TokenKind::Invalid, start, location);
    }
    take(); take();
    if (!ident_start(peek())) {
        diagnostics_.error(location, "expected identifier after '$::'");
        return token(TokenKind::Invalid, start, location);
    }
    for (;;) {
        while (ident_continue(peek())) take();
        if (peek() != ':' || peek(1) != ':' || !ident_start(peek(2))) break;
        take(); take();
    }
    return token(TokenKind::BuiltinName, start, location);
}

Token Lexer::lex_number() {
    const auto start = offset_;
    const SourceLocation location{&source_, offset_, line_, column_};
    bool floating = false;
    while (std::isalnum(static_cast<unsigned char>(peek())) != 0 || peek() == '_' ||
           peek() == '.') {
        if (peek() == '.') floating = true;
        take();
    }
    if ((peek() == '+' || peek() == '-') && offset_ > start) {
        const char previous = source_.text[offset_ - 1];
        if (previous == 'e' || previous == 'E' || previous == 'p' || previous == 'P') {
            floating = true;
            take();
            while (std::isalnum(static_cast<unsigned char>(peek())) != 0) take();
        }
    }
    const std::string_view spelling(source_.text.data() + start, offset_ - start);
    const bool hexadecimal = spelling.starts_with("0x") || spelling.starts_with("0X");
    if (hexadecimal) {
        std::size_t index = 2;
        while (index < spelling.size()) {
            const char ch = spelling[index];
            const bool hex_digit = (ch >= '0' && ch <= '9') ||
                                   (ch >= 'a' && ch <= 'f') ||
                                   (ch >= 'A' && ch <= 'F');
            if (!hex_digit && ch != '_' && ch != '.') break;
            ++index;
        }
        floating = floating || (index < spelling.size() &&
                                (spelling[index] == 'p' || spelling[index] == 'P'));
    } else {
        floating = floating || spelling.find('e') != std::string_view::npos ||
                   spelling.find('E') != std::string_view::npos ||
                   spelling.ends_with("f32") || spelling.ends_with("f64") ||
                   spelling.ends_with("f80") || spelling.ends_with("f128") ||
                   spelling.ends_with("fptr");
    }
    return token(floating ? TokenKind::Floating : TokenKind::Integer, start, location);
}

Token Lexer::lex_quoted(char quote, TokenKind kind) {
    const auto start = offset_;
    const SourceLocation location{&source_, offset_, line_, column_};
    take();
    while (peek() != '\0' && peek() != quote && peek() != '\n') {
        if (take() == '\\' && peek() != '\0') take();
    }
    if (peek() != quote) {
        diagnostics_.error(location, quote == '"' ? "unterminated string literal"
                                                   : "unterminated character literal");
        return token(TokenKind::Invalid, start, location);
    }
    take();
    return token(kind, start, location);
}

Token Lexer::lex_punctuator() {
    static constexpr std::string_view punctuators[] = {
        "<<=", ">>=", "...", "::", "->", "==", "!=", "<=", ">=", "&&", "||",
        "++", "--", "<<", ">>", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=",
        "[[", "]]", "##"
    };
    const auto start = offset_;
    const SourceLocation location{&source_, offset_, line_, column_};
    const auto remaining = std::string_view(source_.text).substr(offset_);
    for (const auto spelling : punctuators) {
        if (remaining.starts_with(spelling)) {
            for (std::size_t i = 0; i < spelling.size(); ++i) take();
            return token(TokenKind::Punctuator, start, location);
        }
    }
    const char ch = take();
    constexpr std::string_view singles = "{}[]();,.:?~!+-*/%<>=&|^#";
    if (singles.find(ch) == std::string_view::npos) {
        diagnostics_.error(location, "invalid source character");
        return token(TokenKind::Invalid, start, location);
    }
    return token(TokenKind::Punctuator, start, location);
}

std::vector<Token> Lexer::lex() {
    std::vector<Token> result;
    if (source_.text.size() >= 3 &&
        static_cast<unsigned char>(source_.text[0]) == 0xef &&
        static_cast<unsigned char>(source_.text[1]) == 0xbb &&
        static_cast<unsigned char>(source_.text[2]) == 0xbf) {
        take(); take(); take();
    }
    while (true) {
        skip_trivia();
        const SourceLocation location{&source_, offset_, line_, column_};
        if (peek() == '\0') {
            result.push_back({TokenKind::End, {}, location});
            break;
        }
        if (ident_start(peek())) result.push_back(lex_identifier());
        else if (peek() == '$') result.push_back(lex_builtin());
        else if (std::isdigit(static_cast<unsigned char>(peek())) != 0) result.push_back(lex_number());
        else if (peek() == '"') result.push_back(lex_quoted('"', TokenKind::String));
        else if (peek() == '\'') result.push_back(lex_quoted('\'', TokenKind::Character));
        else result.push_back(lex_punctuator());
    }
    return result;
}

} // namespace cross
