// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/embed.hpp"

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <optional>
#include <unordered_set>

namespace cross {
namespace {

std::string_view closing(std::string_view opening) {
    if (opening == "(") return ")";
    if (opening == "[") return "]";
    if (opening == "[[") return "]]";
    if (opening == "{") return "}";
    return {};
}

std::optional<std::size_t> group_end(std::span<const Token> tokens,
                                     std::size_t open, std::size_t limit) {
    if (open >= limit || closing(tokens[open].text).empty()) return std::nullopt;
    std::vector<std::string_view> closers{closing(tokens[open].text)};
    for (auto index = open + 1; index < limit; ++index) {
        if (const auto close = closing(tokens[index].text); !close.empty()) {
            closers.push_back(close);
        } else if (tokens[index].is(")") || tokens[index].is("]") ||
                   tokens[index].is("]]") || tokens[index].is("}")) {
            if (closers.back() != tokens[index].text) return std::nullopt;
            closers.pop_back();
            if (closers.empty()) return index;
        }
    }
    return std::nullopt;
}

SourceLocation logical_location(const Token& token,
                                std::span<const SourceLocation> origins) {
    if (token.location.line == 0 || token.location.line > origins.size() ||
        !origins[token.location.line - 1].valid()) return token.location;
    auto result = origins[token.location.line - 1];
    const auto shift = token.location.column - 1;
    if (shift <= result.file->line(result.line).size()) {
        result.column += shift;
        result.offset += shift;
    }
    return result;
}

struct SelectedFile {
    std::filesystem::path path;
    bool regular{};
};

SelectedFile select_file(std::string_view written, SourceLocation logical,
                         const CompilerOptions& options) {
    const std::filesystem::path path{std::string(written)};
    std::vector<std::filesystem::path> candidates;
    if (path.is_absolute()) {
        candidates.push_back(path);
    } else {
        if (logical.file) candidates.push_back(logical.file->path.parent_path() / path);
        for (const auto& directory : options.include_paths)
            candidates.push_back(directory / path);
        for (const auto& directory : options.system_include_paths)
            candidates.push_back(directory / path);
    }
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) || error) continue;
        const bool regular = std::filesystem::is_regular_file(candidate, error) && !error;
        auto selected = std::filesystem::canonical(candidate, error);
        if (error) selected = candidate.lexically_normal();
        return {std::move(selected), regular};
    }
    return {};
}

void visit_embeds(std::span<const Token> tokens,
                  std::span<const SourceLocation> line_origins,
                  Diagnostics& diagnostics,
                  const std::function<void(std::size_t)>& visit) {
    const auto logical = [&](std::size_t index) {
        return logical_location(tokens[index], line_origins);
    };
    std::function<void(std::size_t, std::size_t)> scan;
    scan = [&](std::size_t begin, std::size_t end) {
        for (auto index = begin; index < end;) {
            // A syntax declaration body consists of pattern/activation
            // clauses, not evaluated Cross expressions.
            if (tokens[index].is("syntax") && index + 4 < end &&
                tokens[index + 1].kind == TokenKind::Identifier &&
                tokens[index + 2].is(":") && tokens[index + 4].is("{")) {
                if (const auto close = group_end(tokens, index + 4, end)) {
                    index = *close + 1;
                    continue;
                }
            }
            if (tokens[index].is("$::quote") && index + 1 < end &&
                tokens[index + 1].is("{")) {
                if (const auto close = group_end(tokens, index + 1, end)) {
                    for (auto part = index + 2; part < *close; ++part) {
                        if (!tokens[part].is("$::unquote") ||
                            part + 1 >= *close || !tokens[part + 1].is("(")) continue;
                        if (const auto splice = group_end(tokens, part + 1, *close)) {
                            scan(part + 2, *splice);
                            part = *splice;
                        }
                    }
                    index = *close + 1;
                    continue;
                }
            }
            if (!tokens[index].is("$::embed")) {
                ++index;
                continue;
            }
            if (index + 3 >= end || !tokens[index + 1].is("(") ||
                tokens[index + 2].kind != TokenKind::String ||
                !tokens[index + 3].is(")")) {
                diagnostics.error(logical(index),
                    "$::embed requires exactly one string-literal token");
                ++index;
                continue;
            }
            visit(index);
            index += 4;
        }
    };
    if (!tokens.empty()) scan(0, tokens.size() - 1);
}

} // namespace

EmbedDiscovery discover_embeds(SourceManager& sources, const SourceFile& source,
                               std::span<const SourceLocation> line_origins,
                               const CompilerOptions& options,
                               Diagnostics& diagnostics, bool validate) {
    EmbedDiscovery result{&source, {}};
    if (source.text.find("$::embed") == std::string::npos) return result;

    const auto tokens = Lexer(source, diagnostics).lex();
    if (diagnostics.errors() != 0) return result;
    std::vector<SourceTokenOrigin> token_origins;
    std::unordered_set<std::string> seen_paths;
    visit_embeds(tokens, line_origins, diagnostics, [&](std::size_t index) {
            const auto logical = [&](std::size_t piece) {
                return logical_location(tokens[piece], line_origins);
            };
            const auto written = decode_string_literal(tokens[index + 2].text);
            if (!written || written->find('\0') != std::string::npos) {
                diagnostics.error(logical(index + 2),
                    "$::embed path must be a valid string literal without zero bytes");
                return;
            }
            const auto selected = select_file(*written, logical(index), options);
            if (validate) {
                if (selected.path.empty() || !selected.regular) {
                    diagnostics.error(logical(index),
                        "embedded asset is not a regular file: '" + *written + "'");
                    return;
                }
                std::ifstream check(selected.path, std::ios::binary);
                if (!check) {
                    diagnostics.error(logical(index),
                        "embedded asset is not readable: '" + *written + "'");
                    return;
                }
            }
            auto identity = std::make_shared<EmbedIdentity>(
                EmbedIdentity{*written, selected.path, logical(index)});
            for (unsigned piece = 0; piece < 4; ++piece) {
                const auto& token = tokens[index + piece];
                auto origin = token_origin(token.location);
                origin.span = logical(index + piece);
                origin.embed = identity;
                origin.embed_piece = piece;
                token_origins.push_back({token.location.offset,
                    token.location.offset + token.text.size(), std::move(origin)});
            }
            if (!selected.path.empty() &&
                seen_paths.insert(selected.path.generic_string()).second)
                result.dependencies.push_back(selected.path);
    });
    if (!token_origins.empty()) {
        std::sort(token_origins.begin(), token_origins.end(),
            [](const auto& left, const auto& right) { return left.begin < right.begin; });
        result.source = sources.add(source.path, source.text, {},
                                    std::move(token_origins), source.line_origins,
                                    source.line_units);
    }
    return result;
}

bool validate_embeds(const SourceFile& source, Diagnostics& diagnostics) {
    if (source.text.find("$::embed") == std::string::npos) return true;
    const auto errors = diagnostics.errors();
    const auto tokens = Lexer(source, diagnostics).lex();
    if (diagnostics.errors() != errors) return false;
    visit_embeds(tokens, {}, diagnostics, [&](std::size_t index) {
        const auto first = token_origin(tokens[index].location).embed;
        bool intact = first != nullptr;
        for (unsigned piece = 0; piece < 4 && intact; ++piece) {
            const auto origin = token_origin(tokens[index + piece].location);
            intact = origin.embed == first && origin.embed_piece == piece;
        }
        const auto written = decode_string_literal(tokens[index + 2].text);
        intact = intact && written && *written == first->written_path;
        if (!intact) diagnostics.error(tokens[index].location,
            "generated $::embed expression lacks its declared dependency identity");
    });
    return diagnostics.errors() == errors;
}

bool diagnose_unimplemented_embed_values(const SourceFile& source,
                                          Diagnostics& diagnostics) {
    if (source.text.find("$::embed") == std::string::npos) return true;
    const auto errors = diagnostics.errors();
    const auto tokens = Lexer(source, diagnostics).lex();
    if (diagnostics.errors() != errors) return false;
    visit_embeds(tokens, {}, diagnostics, [&](std::size_t index) {
        const auto origin = token_origin(tokens[index].location);
        diagnostics.error(origin.span,
            "$::embed byte evaluation and static-array materialization "
            "are not implemented yet");
    });
    return diagnostics.errors() == errors;
}

} // namespace cross
