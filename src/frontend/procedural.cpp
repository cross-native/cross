// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/procedural.hpp"

#include "common/source.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross {
namespace {

struct MetaExpression {
    enum class Kind { Variable, Literal, Concat, Template } kind{Kind::Variable};
    std::string literal;
    std::unique_ptr<MetaExpression> left;
    std::unique_ptr<MetaExpression> right;
    std::vector<MetaExpression> parts;
};

struct MetaStatement {
    enum class Kind { Return, Declare, If } kind{Kind::Return};
    std::string name;
    MetaExpression expression;
    bool condition{};
    std::vector<MetaStatement> then_branch;
    std::vector<MetaStatement> else_branch;
    SourceLocation location;
};

struct TokenMacro {
    std::string name;
    std::string parameter;
    std::vector<MetaStatement> body;
    SourceLocation location;
};

struct Replacement {
    std::size_t begin{};
    std::size_t end{};
    std::string text;
    std::string macro_name;
    SourceLocation invocation;
    SourceLocation definition;
};

std::optional<std::size_t> matching_group(const std::vector<Token>& tokens,
                                          std::size_t opening) {
    if (opening >= tokens.size()) return std::nullopt;
    const auto opener = tokens[opening].text;
    const auto closer = opener == "(" ? ")" : opener == "[" ? "]" :
                        opener == "{" ? "}" : std::string_view{};
    if (closer.empty()) return std::nullopt;
    unsigned depth = 0;
    for (std::size_t index = opening; index < tokens.size(); ++index) {
        if (tokens[index].text == opener) ++depth;
        else if (tokens[index].text == closer && --depth == 0) return index;
    }
    return std::nullopt;
}

class MetaParser {
public:
    MetaParser(const std::vector<Token>& tokens, std::size_t begin,
               std::size_t end,
               const std::unordered_set<std::string>& variables,
               Diagnostics& diagnostics)
        : tokens_(tokens), index_(begin), end_(end), variables_(variables),
          diagnostics_(diagnostics) {}

    std::optional<MetaExpression> parse() {
        auto result = expression();
        if (!result || index_ != end_) {
            const auto location = index_ < tokens_.size()
                                      ? tokens_[index_].location
                                      : SourceLocation{};
            diagnostics_.error(
                location,
                "'macro' token expression supports a token variable, "
                "$::quote { ... $::unquote(variable) ... }, "
                "$::meta::parse(string), and "
                "$::meta::concat(left, right)");
            return std::nullopt;
        }
        return result;
    }

private:
    bool consume(std::string_view spelling) {
        if (index_ >= end_ || tokens_[index_].text != spelling) return false;
        ++index_;
        return true;
    }

    std::optional<MetaExpression> expression() {
        if (index_ >= end_) return std::nullopt;
        if (tokens_[index_].kind == TokenKind::Identifier &&
            variables_.contains(std::string(tokens_[index_].text))) {
            MetaExpression result;
            result.literal = std::string(tokens_[index_].text);
            ++index_;
            return result;
        }
        if (tokens_[index_].text == "$::meta::parse") {
            ++index_;
            if (!consume("(") || index_ >= end_ ||
                tokens_[index_].kind != TokenKind::String) {
                return std::nullopt;
            }
            const auto decoded = decode_string_literal(tokens_[index_++].text);
            if (!decoded || !consume(")")) return std::nullopt;
            MetaExpression result;
            result.kind = MetaExpression::Kind::Literal;
            result.literal = *decoded;
            return result;
        }
        if (tokens_[index_].text == "$::meta::concat") {
            ++index_;
            if (!consume("(")) return std::nullopt;
            auto left = expression();
            if (!left || !consume(",")) return std::nullopt;
            auto right = expression();
            if (!right || !consume(")")) return std::nullopt;
            MetaExpression result;
            result.kind = MetaExpression::Kind::Concat;
            result.left = std::make_unique<MetaExpression>(std::move(*left));
            result.right = std::make_unique<MetaExpression>(std::move(*right));
            return result;
        }
        if (tokens_[index_].text == "$::quote") {
            ++index_;
            if (index_ >= end_ || tokens_[index_].text != "{") {
                return std::nullopt;
            }
            const auto opening = index_;
            const auto closing = matching_group(tokens_, opening);
            if (!closing || *closing >= end_) return std::nullopt;
            const auto* file = tokens_[opening].location.file;
            if (!file || tokens_[*closing].location.file != file) {
                return std::nullopt;
            }

            MetaExpression result;
            result.kind = MetaExpression::Kind::Template;
            auto literal_begin =
                tokens_[opening].location.offset +
                tokens_[opening].text.size();
            auto append_literal = [&](std::size_t end) {
                if (end <= literal_begin) return;
                MetaExpression literal;
                literal.kind = MetaExpression::Kind::Literal;
                literal.literal =
                    std::string_view(file->text)
                        .substr(literal_begin, end - literal_begin);
                result.parts.push_back(std::move(literal));
            };

            for (auto cursor = opening + 1;
                 cursor < *closing;) {
                if (tokens_[cursor].text == "$::unquote" &&
                    cursor + 3 < *closing &&
                    tokens_[cursor + 1].text == "(" &&
                    tokens_[cursor + 2].kind ==
                        TokenKind::Identifier &&
                    variables_.contains(
                        std::string(tokens_[cursor + 2].text)) &&
                    tokens_[cursor + 3].text == ")") {
                    append_literal(tokens_[cursor].location.offset);
                    MetaExpression variable;
                    variable.literal = std::string(tokens_[cursor + 2].text);
                    result.parts.push_back(std::move(variable));
                    literal_begin =
                        tokens_[cursor + 3].location.offset +
                        tokens_[cursor + 3].text.size();
                    cursor += 4;
                    continue;
                }
                if (tokens_[cursor].text == "$::unquote") {
                    diagnostics_.error(tokens_[cursor].location,
                                       "$::unquote requires a visible token variable inside $::quote");
                    return std::nullopt;
                }
                ++cursor;
            }
            append_literal(tokens_[*closing].location.offset);
            index_ = *closing + 1;
            return result;
        }
        return std::nullopt;
    }

    const std::vector<Token>& tokens_;
    std::size_t index_{};
    std::size_t end_{};
    const std::unordered_set<std::string>& variables_;
    Diagnostics& diagnostics_;
};

std::optional<std::size_t> meta_statement_end(
    const std::vector<Token>& tokens, std::size_t begin, std::size_t end) {
    for (auto cursor = begin; cursor < end; ++cursor) {
        if (tokens[cursor].text == ";") return cursor;
        if (tokens[cursor].text == "(" || tokens[cursor].text == "[" ||
            tokens[cursor].text == "{") {
            const auto closing = matching_group(tokens, cursor);
            if (!closing || *closing >= end) return std::nullopt;
            cursor = *closing;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<MetaStatement>> parse_meta_body(
    const std::vector<Token>& tokens, std::size_t begin, std::size_t end,
    std::unordered_set<std::string> variables, Diagnostics& diagnostics,
    unsigned depth = 0) {
    if (depth > 128) {
        diagnostics.error(tokens[begin].location,
                          "procedural macro body exceeds the nesting limit");
        return std::nullopt;
    }
    std::vector<MetaStatement> result;
    for (auto cursor = begin; cursor < end;) {
        MetaStatement statement;
        statement.location = tokens[cursor].location;
        if (tokens[cursor].text == "if") {
            if (cursor + 1 >= end || tokens[cursor + 1].text != "(") {
                diagnostics.error(statement.location,
                                  "procedural macro 'if' requires a condition");
                return std::nullopt;
            }
            const auto condition_end = matching_group(tokens, cursor + 1);
            if (!condition_end || *condition_end != cursor + 3 ||
                *condition_end + 1 >= end ||
                tokens[*condition_end + 1].text != "{") {
                diagnostics.error(statement.location,
                                  "procedural macro 'if' currently requires a literal condition and braced body");
                return std::nullopt;
            }
            const auto condition = tokens[cursor + 2].text;
            if (condition == "true" || condition == "1") {
                statement.condition = true;
            } else if (condition == "false" || condition == "0") {
                statement.condition = false;
            } else {
                diagnostics.error(tokens[cursor + 2].location,
                                  "procedural macro 'if' condition currently requires a literal bool or 0/1");
                return std::nullopt;
            }
            statement.kind = MetaStatement::Kind::If;
            const auto then_open = *condition_end + 1;
            const auto then_end = matching_group(tokens, then_open);
            if (!then_end || *then_end >= end) {
                diagnostics.error(tokens[then_open].location,
                                  "unterminated procedural macro 'if' body");
                return std::nullopt;
            }
            auto then_body = parse_meta_body(tokens, then_open + 1,
                                             *then_end, variables,
                                             diagnostics, depth + 1);
            if (!then_body) return std::nullopt;
            statement.then_branch = std::move(*then_body);
            cursor = *then_end + 1;
            if (cursor < end && tokens[cursor].text == "else") {
                if (cursor + 1 >= end || tokens[cursor + 1].text != "{") {
                    diagnostics.error(tokens[cursor].location,
                                      "procedural macro 'else' requires a braced body");
                    return std::nullopt;
                }
                const auto else_open = cursor + 1;
                const auto else_end = matching_group(tokens, else_open);
                if (!else_end || *else_end >= end) {
                    diagnostics.error(tokens[else_open].location,
                                      "unterminated procedural macro 'else' body");
                    return std::nullopt;
                }
                auto else_body = parse_meta_body(tokens, else_open + 1,
                                                 *else_end, variables,
                                                 diagnostics, depth + 1);
                if (!else_body) return std::nullopt;
                statement.else_branch = std::move(*else_body);
                cursor = *else_end + 1;
            }
            result.push_back(std::move(statement));
            continue;
        }
        const bool declaration = tokens[cursor].text == "$::meta::tokens";
        const bool returning = tokens[cursor].text == "return";
        if (!declaration && !returning) {
            diagnostics.error(statement.location,
                              "procedural macro body currently supports token locals, braced if/else, and return");
            return std::nullopt;
        }
        auto expression_begin = cursor + 1;
        if (declaration) {
            if (expression_begin + 1 >= end ||
                tokens[expression_begin].kind != TokenKind::Identifier ||
                tokens[expression_begin + 1].text != "=") {
                diagnostics.error(statement.location,
                                  "procedural macro token local requires a name and initializer");
                return std::nullopt;
            }
            statement.kind = MetaStatement::Kind::Declare;
            statement.name = std::string(tokens[expression_begin].text);
            if (variables.contains(statement.name)) {
                diagnostics.error(tokens[expression_begin].location,
                                  "procedural macro token local redeclares '" +
                                      statement.name + "'");
                return std::nullopt;
            }
            expression_begin += 2;
        }
        const auto semicolon = meta_statement_end(tokens, expression_begin,
                                                   end);
        if (!semicolon) {
            diagnostics.error(statement.location,
                              "unterminated procedural macro statement");
            return std::nullopt;
        }
        MetaParser parser(tokens, expression_begin, *semicolon, variables,
                          diagnostics);
        auto expression = parser.parse();
        if (!expression) return std::nullopt;
        statement.expression = std::move(*expression);
        if (declaration) variables.insert(statement.name);
        result.push_back(std::move(statement));
        cursor = *semicolon + 1;
    }
    return result;
}

using MetaEnvironment = std::unordered_map<std::string, std::string>;

std::optional<std::string> evaluate(const MetaExpression& expression,
                                    const MetaEnvironment& variables) {
    switch (expression.kind) {
    case MetaExpression::Kind::Variable: {
        const auto found = variables.find(expression.literal);
        if (found == variables.end()) return std::nullopt;
        return found->second;
    }
    case MetaExpression::Kind::Literal:
        return expression.literal;
    case MetaExpression::Kind::Concat: {
        auto left = evaluate(*expression.left, variables);
        auto right = evaluate(*expression.right, variables);
        if (!left || !right) return std::nullopt;
        return *left + *right;
    }
    case MetaExpression::Kind::Template: {
        std::string result;
        for (const auto& part : expression.parts) {
            auto value = evaluate(part, variables);
            if (!value) return std::nullopt;
            result += *value;
        }
        return result;
    }
    }
    return std::nullopt;
}

std::optional<std::string> execute_meta_body(
    const std::vector<MetaStatement>& body, MetaEnvironment& variables) {
    for (const auto& statement : body) {
        if (statement.kind == MetaStatement::Kind::If) {
            auto branch_variables = variables;
            const auto& branch = statement.condition
                ? statement.then_branch : statement.else_branch;
            if (auto result = execute_meta_body(branch, branch_variables)) {
                return result;
            }
            continue;
        }
        auto value = evaluate(statement.expression, variables);
        if (!value) return std::nullopt;
        if (statement.kind == MetaStatement::Kind::Return) return value;
        variables[statement.name] = std::move(*value);
    }
    return std::nullopt;
}

std::string qualified_name(const std::vector<Token>& tokens, std::size_t& index,
                           std::size_t end) {
    if (index >= end || tokens[index].kind != TokenKind::Identifier) return {};
    std::string result(tokens[index++].text);
    while (index + 1 < end && tokens[index].text == "::" &&
           tokens[index + 1].kind == TokenKind::Identifier) {
        result += "::";
        result += tokens[index + 1].text;
        index += 2;
    }
    return result;
}

struct NamespaceRegion {
    std::size_t begin{};
    std::size_t end{};
    std::string name;
};

void collect_namespace_regions(const std::vector<Token>& tokens,
                               std::size_t begin, std::size_t end,
                               std::string_view parent,
                               std::vector<NamespaceRegion>& regions) {
    for (auto index = begin; index < end;) {
        if (tokens[index].text != "namespace") {
            ++index;
            continue;
        }
        auto cursor = index + 1;
        auto name = qualified_name(tokens, cursor, end);
        if (name.empty() || cursor >= end || tokens[cursor].text != "{") {
            ++index;
            continue;
        }
        const auto closing = matching_group(tokens, cursor);
        if (!closing || *closing > end) {
            ++index;
            continue;
        }
        if (!parent.empty()) {
            name = std::string(parent) + "::" + name;
        }
        regions.push_back({cursor + 1, *closing, name});
        collect_namespace_regions(tokens, cursor + 1, *closing, name, regions);
        index = *closing + 1;
    }
}

std::vector<NamespaceRegion> namespace_regions(
    const std::vector<Token>& tokens) {
    std::vector<NamespaceRegion> result;
    collect_namespace_regions(tokens, 0, tokens.size(), {}, result);
    return result;
}

std::string namespace_at(const std::vector<NamespaceRegion>& regions,
                         std::size_t index) {
    const NamespaceRegion* best{};
    for (const auto& region : regions) {
        if (index < region.begin || index >= region.end) continue;
        if (!best || region.end - region.begin < best->end - best->begin) {
            best = &region;
        }
    }
    return best ? best->name : std::string{};
}

std::vector<std::string> namespace_prefixes(std::string current) {
    std::vector<std::string> result;
    while (!current.empty()) {
        result.push_back(current);
        const auto separator = current.rfind("::");
        if (separator == std::string::npos) break;
        current.resize(separator);
    }
    return result;
}

std::vector<std::string> active_imports(const std::vector<Token>& tokens,
                                        std::size_t use) {
    struct ActiveImport {
        std::string name;
        std::size_t scope_size{};
        std::size_t order{};
    };
    std::vector<ActiveImport> active;
    for (std::size_t index = 0; index < use; ++index) {
        if (tokens[index].text != "using") continue;
        auto cursor = index + 1;
        auto name = qualified_name(tokens, cursor, tokens.size());
        if (name.empty() || cursor >= tokens.size() ||
            tokens[cursor].text != ";") {
            continue;
        }
        const auto visible_begin = cursor + 1;
        if (visible_begin > use) continue;
        auto visible_end = tokens.size();
        for (std::size_t opening = 0; opening < index; ++opening) {
            if (tokens[opening].text != "{") continue;
            const auto closing = matching_group(tokens, opening);
            if (closing && opening < index && index < *closing &&
                *closing < visible_end) {
                visible_end = *closing;
            }
        }
        if (use < visible_end) {
            active.push_back(
                {std::move(name), visible_end - visible_begin, index});
        }
        index = cursor;
    }
    std::stable_sort(
        active.begin(), active.end(),
        [](const ActiveImport& left, const ActiveImport& right) {
            if (left.scope_size != right.scope_size) {
                return left.scope_size < right.scope_size;
            }
            return left.order < right.order;
        });
    std::vector<std::string> result;
    for (auto& item : active) result.push_back(std::move(item.name));
    return result;
}

std::vector<TokenMacro> collect_macros(const std::vector<Token>& tokens,
                                       std::vector<Replacement>& removals,
                                       Diagnostics& diagnostics) {
    std::vector<TokenMacro> macros;
    const auto regions = namespace_regions(tokens);
    for (std::size_t index = 0; index + 3 < tokens.size(); ++index) {
        if (tokens[index].text != "[[" ||
            tokens[index + 1].text != "macro" ||
            tokens[index + 2].text != "]]") {
            continue;
        }
        const auto declaration_begin = tokens[index].location.offset;
        auto cursor = index + 3;
        bool static_storage = false;
        bool global_storage = false;
        while (cursor < tokens.size() &&
               (tokens[cursor].text == "static" ||
                tokens[cursor].text == "global" ||
                tokens[cursor].text == "inline")) {
            static_storage = static_storage ||
                             tokens[cursor].text == "static";
            global_storage = global_storage ||
                             tokens[cursor].text == "global";
            ++cursor;
        }
        if (!static_storage || global_storage) {
            diagnostics.error(
                tokens[index].location,
                "'macro' functions must be static and cannot be global");
            continue;
        }
        if (cursor >= tokens.size() ||
            tokens[cursor].text != "$::meta::tokens") {
            diagnostics.error(tokens[index].location,
                              "'macro' functions must return $::meta::tokens");
            continue;
        }
        ++cursor;
        auto name = qualified_name(tokens, cursor, tokens.size());
        if (name.empty() || cursor >= tokens.size() ||
            tokens[cursor].text != "(") {
            diagnostics.error(tokens[index].location,
                              "malformed 'macro' declaration");
            continue;
        }
        const auto current_namespace = namespace_at(regions, index);
        if (!current_namespace.empty()) {
            name = current_namespace + "::" + name;
        }
        const auto parameter_end = matching_group(tokens, cursor);
        if (!parameter_end) {
            diagnostics.error(tokens[cursor].location,
                              "unterminated 'macro' parameter list");
            continue;
        }
        if (*parameter_end != cursor + 4 ||
            tokens[cursor + 1].text != "in" ||
            tokens[cursor + 2].text != "$::meta::tokens" ||
            tokens[cursor + 3].kind != TokenKind::Identifier) {
            diagnostics.error(
                tokens[cursor].location,
                "'macro' requires exactly one 'in $::meta::tokens name' "
                "parameter");
            continue;
        }
        std::string parameter(tokens[cursor + 3].text);
        cursor = *parameter_end + 1;
        if (parameter.empty() || cursor >= tokens.size() ||
            tokens[cursor].text != "{") {
            diagnostics.error(tokens[index].location,
                              "'macro' requires one named token parameter and a body");
            continue;
        }
        const auto body_end = matching_group(tokens, cursor);
        if (!body_end) {
            diagnostics.error(tokens[cursor].location,
                              "unterminated 'macro' body");
            continue;
        }
        const auto body_begin = cursor + 1;
        auto body = parse_meta_body(tokens, body_begin, *body_end,
                                    {parameter}, diagnostics);
        if (!body) continue;
        const auto declaration_end =
            tokens[*body_end].location.offset + tokens[*body_end].text.size();
        removals.push_back(
            {declaration_begin, declaration_end, {}, {}, {}, {}});
        if (std::any_of(macros.begin(), macros.end(),
                        [&](const TokenMacro& macro) {
                            return macro.name == name;
                        })) {
            diagnostics.error(tokens[index].location,
                              "procedural macro '" + name +
                                  "' is defined more than once");
            continue;
        }
        macros.push_back({std::move(name), std::move(parameter),
                          std::move(*body), tokens[index].location});
        index = *body_end;
    }
    return macros;
}

void apply_replacements(std::string& source,
                        std::vector<Replacement> replacements) {
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement& left, const Replacement& right) {
                  return left.begin > right.begin;
              });
    for (auto& replacement : replacements) {
        source.replace(replacement.begin, replacement.end - replacement.begin,
                       replacement.text);
    }
}

const TokenMacro* find_macro(const std::vector<TokenMacro>& macros,
                             std::string_view written_name,
                             std::string current_namespace,
                             const std::vector<std::string>& imports) {
    const auto exact = [&](std::string_view name) -> const TokenMacro* {
        const auto found = std::find_if(
            macros.begin(), macros.end(), [&](const TokenMacro& macro) {
                return macro.name == name;
            });
        return found == macros.end() ? nullptr : &*found;
    };
    for (const auto& prefix :
         namespace_prefixes(std::move(current_namespace))) {
        if (const auto* macro =
                exact(prefix + "::" + std::string(written_name))) {
            return macro;
        }
    }
    for (const auto& imported : imports) {
        if (const auto* macro =
                exact(imported + "::" + std::string(written_name))) {
            return macro;
        }
    }
    return exact(written_name);
}

std::optional<Replacement> find_expansion(const SourceFile& file,
                                          const std::vector<Token>& tokens,
                                           const std::vector<TokenMacro>& macros,
                                           Diagnostics& diagnostics) {
    const auto regions = namespace_regions(tokens);
    for (std::size_t index = 0; index + 2 < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::Identifier) {
            auto cursor = index;
            const auto name = qualified_name(tokens, cursor, tokens.size());
            const auto current_namespace = namespace_at(regions, index);
            const auto imports = active_imports(tokens, index);
            if (const auto* macro =
                    find_macro(macros, name, current_namespace, imports);
                macro && cursor + 1 < tokens.size() &&
                tokens[cursor].text == "!" &&
                (tokens[cursor + 1].text == "(" ||
                 tokens[cursor + 1].text == "[" ||
                 tokens[cursor + 1].text == "{")) {
                const auto close = matching_group(tokens, cursor + 1);
                if (!close) {
                    diagnostics.error(tokens[cursor + 1].location,
                                      "unterminated procedural macro token tree");
                    return std::nullopt;
                }
                const auto input_begin = tokens[cursor + 1].location.offset +
                                         tokens[cursor + 1].text.size();
                const auto input_end = tokens[*close].location.offset;
                const auto replacement_end = tokens[*close].location.offset +
                                             tokens[*close].text.size();
                MetaEnvironment variables;
                variables.emplace(macro->parameter,
                    std::string(std::string_view(file.text).substr(
                        input_begin, input_end - input_begin)));
                auto output = execute_meta_body(macro->body, variables);
                if (!output) {
                    diagnostics.error(tokens[index].location,
                                      "procedural macro '" + macro->name +
                                          "' did not return a token value");
                    return std::nullopt;
                }
                return Replacement{
                    tokens[index].location.offset, replacement_end,
                    std::move(*output),
                    macro->name, tokens[index].location, macro->location};
            }
        }
    }
    return std::nullopt;
}

std::vector<SourceExpansion> remap_expansions(
    const SourceFile& source, const Replacement& replacement) {
    std::vector<SourceExpansion> result;
    const auto removed = replacement.end - replacement.begin;
    const auto inserted = replacement.text.size();
    const auto delta = static_cast<std::int64_t>(inserted) -
                       static_cast<std::int64_t>(removed);
    for (auto expansion : source.expansions) {
        if (expansion.end <= replacement.begin) {
            result.push_back(std::move(expansion));
            continue;
        }
        if (expansion.begin >= replacement.end) {
            expansion.begin = static_cast<std::size_t>(
                static_cast<std::int64_t>(expansion.begin) + delta);
            expansion.end = static_cast<std::size_t>(
                static_cast<std::int64_t>(expansion.end) + delta);
            result.push_back(std::move(expansion));
            continue;
        }
        if (expansion.begin < replacement.begin &&
            expansion.end > replacement.end) {
            expansion.end = static_cast<std::size_t>(
                static_cast<std::int64_t>(expansion.end) + delta);
            result.push_back(std::move(expansion));
            continue;
        }
        if (expansion.begin < replacement.begin) {
            expansion.end = replacement.begin;
            if (expansion.begin < expansion.end) {
                result.push_back(std::move(expansion));
            }
            continue;
        }
        if (expansion.end > replacement.end) {
            expansion.begin = replacement.begin + inserted;
            expansion.end = static_cast<std::size_t>(
                static_cast<std::int64_t>(expansion.end) + delta);
            if (expansion.begin < expansion.end) {
                result.push_back(std::move(expansion));
            }
        }
    }
    if (!replacement.text.empty()) {
        result.push_back(
            {replacement.begin, replacement.begin + replacement.text.size(),
             replacement.macro_name, replacement.invocation,
             replacement.definition});
    }
    return result;
}

} // namespace

const SourceFile* expand_procedural_macros(SourceManager& sources,
                                           const std::filesystem::path& path,
                                           std::string_view source,
                                           Diagnostics& diagnostics) {
    std::string result(source);
    const auto* definition_file = sources.add(path, result);
    Lexer definition_lexer(*definition_file, diagnostics);
    const auto definition_tokens = definition_lexer.lex();
    std::vector<Replacement> removals;
    auto macros = collect_macros(definition_tokens, removals, diagnostics);
    if (diagnostics.errors() != 0) return definition_file;
    apply_replacements(result, std::move(removals));
    auto* current = sources.add(path, result);
    if (macros.empty()) return current;

    for (unsigned expansion = 0; expansion < 128; ++expansion) {
        Lexer lexer(*current, diagnostics);
        const auto tokens = lexer.lex();
        if (diagnostics.errors() != 0) return current;
        auto replacement =
            find_expansion(*current, tokens, macros, diagnostics);
        if (!replacement) return current;
        auto origins = remap_expansions(*current, *replacement);
        result.replace(replacement->begin, replacement->end - replacement->begin,
                       replacement->text);
        current = sources.add(path, result, std::move(origins));
    }
    diagnostics.command_error(
        "procedural macro expansion exceeded 128 explicit invocations");
    return current;
}

} // namespace cross
