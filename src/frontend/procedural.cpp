// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/procedural.hpp"

#include "common/source.hpp"
#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cross {
namespace {

struct TokenMacro {
    std::string name;
    FunctionDecl function;
    SourceLocation location;
    bool syntax_expander{};
};

struct Replacement {
    std::size_t begin{};
    std::size_t end{};
    std::string text;
    std::string macro_name;
    SourceLocation invocation;
    SourceLocation definition;
    std::vector<SourceTokenOrigin> token_origins{};
};

std::optional<std::size_t> matching_group(const std::vector<Token>& tokens,
                                          std::size_t opening) {
    if (opening >= tokens.size()) return std::nullopt;
    const auto opener = tokens[opening].text;
    const auto closer = opener == "(" ? ")" : opener == "[" ? "]" :
                        opener == "[[" ? "]]" : opener == "{" ? "}" : std::string_view{};
    if (closer.empty()) return std::nullopt;
    // Find the enclosing declaration/group without diagnosing its contents.
    // Quote and raw-input validation own mixed-delimiter diagnostics later.
    unsigned depth = 0;
    for (std::size_t index = opening; index < tokens.size(); ++index) {
        if (tokens[index].text == opener) ++depth;
        else if (tokens[index].text == closer && --depth == 0) return index;
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
        std::size_t scope_begin = 0;
        for (std::size_t opening = 0; opening < index; ++opening) {
            if (tokens[opening].text != "{") continue;
            const auto closing = matching_group(tokens, opening);
            if (closing && opening < index && index < *closing &&
                *closing < visible_end) {
                visible_end = *closing;
                scope_begin = opening;
            }
        }
        if (use < visible_end) {
            active.push_back(
                {std::move(name), visible_end - scope_begin, index});
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
                                       Diagnostics& diagnostics,
                                       unsigned address_bits,
                                       bool syntax_expanders = false) {
    std::vector<TokenMacro> macros;
    const auto regions = namespace_regions(tokens);
    for (std::size_t index = 0; index + 3 < tokens.size(); ++index) {
        if (tokens[index].text != "[[" ||
            (tokens[index + 1].text != "macro" &&
             !(syntax_expanders && tokens[index + 1].text == "syntax_expander")) ||
            tokens[index + 2].text != "]]") {
            continue;
        }
        const bool syntax_expander = tokens[index + 1].text == "syntax_expander";
        const auto role = syntax_expander ? "syntax_expander" : "macro";
        const auto input_type = syntax_expander ? "$::meta::syntax_match" : "$::meta::tokens";
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
                "'" + std::string(role) + "' functions must be static and cannot be global");
            continue;
        }
        if (cursor >= tokens.size() ||
            tokens[cursor].text != "$::meta::tokens") {
            diagnostics.error(tokens[index].location,
                              "'" + std::string(role) + "' functions must return $::meta::tokens");
            continue;
        }
        ++cursor;
        auto name = qualified_name(tokens, cursor, tokens.size());
        if (name.empty() || cursor >= tokens.size() ||
            tokens[cursor].text != "(") {
            diagnostics.error(tokens[index].location,
                              "malformed '" + std::string(role) + "' declaration");
            continue;
        }
        const auto current_namespace = namespace_at(regions, index);
        if (!current_namespace.empty()) {
            name = current_namespace + "::" + name;
        }
        const auto parameter_end = matching_group(tokens, cursor);
        if (!parameter_end) {
            diagnostics.error(tokens[cursor].location,
                              "unterminated '" + std::string(role) + "' parameter list");
            continue;
        }
        const bool valid_length =
            *parameter_end == cursor + 4 ||
            *parameter_end == cursor + 5;
        if (!valid_length) {
            diagnostics.error(
                tokens[cursor].location,
                "'" + std::string(role) + "' requires exactly one 'in [const] " +
                std::string(input_type) + " name' parameter");
            continue;
        }
        const bool local_const =
            *parameter_end == cursor + 5 &&
            tokens[cursor + 2].text == "const";
        const auto type_index = cursor + (local_const ? 3 : 2);
        const auto name_index = type_index + 1;
        if ((*parameter_end != cursor + 4 && !local_const) ||
            tokens[cursor + 1].text != "in" ||
            tokens[type_index].text != input_type ||
            tokens[name_index].kind != TokenKind::Identifier) {
            diagnostics.error(
                tokens[cursor].location,
                "expansion function requires exactly one 'in [const] " +
                    std::string(input_type) + " name' parameter");
            continue;
        }
        std::string parameter(tokens[name_index].text);
        cursor = *parameter_end + 1;
        if (parameter.empty() || cursor >= tokens.size() ||
            tokens[cursor].text != "{") {
            diagnostics.error(tokens[index].location,
                              "'" + std::string(role) + "' requires one named parameter and a body");
            continue;
        }
        const auto body_end = matching_group(tokens, cursor);
        if (!body_end) {
            diagnostics.error(tokens[cursor].location,
                              "unterminated '" + std::string(role) + "' body");
            continue;
        }
        FunctionDecl function;
        function.name = name;
        function.location = tokens[index].location;
        function.return_type = tokens_type();
        function.source_namespace = current_namespace;
        function.imports = active_imports(tokens, index);
        function.linkage = Linkage::Static;
        auto parameter_type = syntax_expander ? syntax_match_type() : tokens_type();
        parameter_type->is_const = local_const;
        function.parameters.push_back({tokens[*parameter_end - 1].location,
            std::move(parameter), std::move(parameter_type), ParameterMode::In, true, {}});
        std::vector<Token> body_tokens(tokens.begin() + static_cast<std::ptrdiff_t>(cursor),
            tokens.begin() + static_cast<std::ptrdiff_t>(*body_end + 1));
        body_tokens.push_back({TokenKind::End, {}, tokens[*body_end].location});
        Parser parser(std::move(body_tokens), diagnostics, {}, address_bits);
        auto body = parser.parse_procedural_body(function);
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
        function.body = std::move(body);
        macros.push_back({std::move(name), std::move(function), tokens[index].location,
                          syntax_expander});
        index = *body_end;
    }
    return macros;
}

std::vector<SourceTokenOrigin> remap_token_origins(
    const std::vector<SourceTokenOrigin>& source, const Replacement& replacement) {
    std::vector<SourceTokenOrigin> result;
    const auto delta = static_cast<std::int64_t>(replacement.text.size()) -
        static_cast<std::int64_t>(replacement.end - replacement.begin);
    for (auto token : source) {
        if (token.end <= replacement.begin) {
            result.push_back(std::move(token));
        } else if (token.begin >= replacement.end) {
            token.begin = static_cast<std::size_t>(static_cast<std::int64_t>(token.begin) + delta);
            token.end = static_cast<std::size_t>(static_cast<std::int64_t>(token.end) + delta);
            result.push_back(std::move(token));
        }
    }
    for (auto token : replacement.token_origins) {
        token.begin += replacement.begin;
        token.end += replacement.begin;
        result.push_back(std::move(token));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.begin < right.begin;
    });
    return result;
}

void apply_replacements(std::string& source, std::vector<SourceTokenOrigin>& origins,
                        std::vector<Replacement> replacements) {
    std::sort(replacements.begin(), replacements.end(),
              [](const Replacement& left, const Replacement& right) {
                  return left.begin > right.begin;
              });
    for (auto& replacement : replacements) {
        origins = remap_token_origins(origins, replacement);
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

std::optional<Replacement> find_expansion(SourceManager& sources,
                                          const std::vector<Token>& tokens,
                                           const std::vector<TokenMacro>& macros,
                                           Diagnostics& diagnostics,
                                           unsigned address_bits,
                                           const LayoutQuery& size_of,
                                           const LayoutQuery& align_of,
                                           EvaluationLimits limits,
                                           EvaluationLayout layout) {
    const auto regions = namespace_regions(tokens);
    for (std::size_t index = 0; index + 2 < tokens.size(); ++index) {
        if (tokens[index].kind == TokenKind::Identifier) {
            auto cursor = index;
            const auto name = qualified_name(tokens, cursor, tokens.size());
            const auto source_origin = token_origin(tokens[index].location);
            const auto current_namespace = source_origin.context
                ? source_origin.context->name_space : namespace_at(regions, index);
            const auto imports = source_origin.context
                ? source_origin.context->imports : active_imports(tokens, index);
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
                const auto replacement_end = tokens[*close].location.offset +
                                             tokens[*close].text.size();
                const auto expansion = sources.next_expansion();
                auto call_context = std::make_shared<SyntaxContext>();
                call_context->kind = SyntaxContext::Kind::CallSite;
                call_context->expansion = expansion;
                call_context->invocation = tokens[index].location;
                call_context->name_space = current_namespace;
                call_context->imports = imports;
                TokenSequence input;
                for (auto item = cursor + 2; item < *close; ++item) {
                    MetaToken token(tokens[item]);
                    if (!token.origin.context) token.origin.context = call_context;
                    input.push_back(std::move(token));
                }
                auto definition_context = std::make_shared<SyntaxContext>();
                definition_context->kind = SyntaxContext::Kind::DefinitionSite;
                definition_context->expansion = expansion;
                definition_context->definition = macro->location;
                definition_context->invocation = tokens[index].location;
                definition_context->name_space = macro->function.source_namespace;
                definition_context->imports = macro->function.imports;
                auto output = evaluate_procedural_body(macro->function,
                    input, address_bits, size_of, align_of, definition_context,
                    diagnostics, limits, layout, {}, call_context);
                if (!output) {
                    diagnostics.error(tokens[index].location,
                                      "procedural macro '" + macro->name +
                                          "' did not return a token value");
                    return std::nullopt;
                }
                Replacement replacement{tokens[index].location.offset, replacement_end,
                    " ", macro->name, tokens[index].location, macro->location};
                for (std::size_t position = 0; position < output->size(); ++position) {
                    auto& token = (*output)[position];
                    if (!token.origin.identity.source_unit) {
                        token.origin.identity = {source_origin.identity.source_unit, 0,
                                                 expansion, position};
                    }
                    const auto begin = replacement.text.size();
                    replacement.text += token.text;
                    replacement.token_origins.push_back({begin, replacement.text.size(), token.origin, token.splice});
                    // Serialization is the only text boundary. Never paste two
                    // adjacent token spellings into a different lexical token.
                    replacement.text += ' ';
                }
                return replacement;
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

std::optional<ExpansionFunctionSource> parse_expansion_function(
    const std::vector<Token>& tokens, std::size_t& index, Diagnostics& diagnostics,
    unsigned address_bits) {
    const auto begin = index;
    auto end = index;
    for (auto at = index + 3; at < tokens.size() && tokens[at].kind != TokenKind::End; ++at) {
        if (tokens[at].is("{")) {
            const auto close = matching_group(tokens, at);
            end = close ? *close + 1 : tokens.size() - 1;
            break;
        }
        if (tokens[at].is(";")) { end = at + 1; break; }
    }
    if (end == begin) end = tokens.size() - 1;
    std::vector<Token> bounded(tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                               tokens.begin() + static_cast<std::ptrdiff_t>(end));
    bounded.push_back({TokenKind::End, {}, tokens[end < tokens.size() ? end : tokens.size() - 1].location});
    index = end;
    std::vector<Replacement> removals;
    auto functions = collect_macros(bounded, removals, diagnostics,
                                    address_bits, true);
    if (functions.size() != 1 || diagnostics.errors() != 0) return {};
    return ExpansionFunctionSource{std::move(functions.front().function), functions.front().syntax_expander};
}

const SourceFile* expand_procedural_macros(SourceManager& sources,
                                           const SourceFile& source,
                                           Diagnostics& diagnostics,
                                           unsigned address_bits,
                                           const LayoutQuery& size_of,
                                           const LayoutQuery& align_of,
                                           EvaluationLimits limits,
                                           EvaluationLayout layout) {
    std::string result(source.text);
    const auto* definition_file = &source;
    Lexer definition_lexer(*definition_file, diagnostics);
    const auto definition_tokens = definition_lexer.lex();
    std::vector<Replacement> removals;
    auto macros = collect_macros(definition_tokens, removals, diagnostics,
                                 address_bits);
    if (diagnostics.errors() != 0) return definition_file;
    if (macros.empty()) return definition_file;
    std::vector<SourceTokenOrigin> token_origins;
    for (const auto& token : definition_tokens) {
        if (token.kind != TokenKind::End)
            token_origins.push_back({token.location.offset,
                token.location.offset + token.text.size(), token_origin(token.location)});
    }
    apply_replacements(result, token_origins, std::move(removals));
    const auto unit_lines = [&](std::string_view text) {
        if (source.line_units.empty()) return std::vector<std::string>{};
        const auto count = static_cast<std::size_t>(
            std::count(text.begin(), text.end(), '\n')) +
            (text.empty() || text.back() == '\n' ? 0U : 1U);
        return std::vector<std::string>(count, source.source_unit_at(1));
    };
    auto* current = sources.add(source.path, result, {},
                                std::move(token_origins), {}, unit_lines(result));

    for (unsigned expansion = 0; expansion < 128; ++expansion) {
        Lexer lexer(*current, diagnostics);
        const auto tokens = lexer.lex();
        if (diagnostics.errors() != 0) return current;
        auto replacement =
            find_expansion(sources, tokens, macros, diagnostics,
                           address_bits, size_of, align_of, limits, layout);
        if (!replacement) return current;
        auto origins = remap_expansions(*current, *replacement);
        token_origins = remap_token_origins(current->token_origins, *replacement);
        result.replace(replacement->begin, replacement->end - replacement->begin,
                       replacement->text);
        current = sources.add(source.path, result, std::move(origins),
                              std::move(token_origins), {}, unit_lines(result));
    }
    diagnostics.command_error(
        "procedural macro expansion exceeded 128 explicit invocations");
    return current;
}

const SourceFile* expand_procedural_macros(SourceManager& sources,
                                           const std::filesystem::path& path,
                                           std::string_view source,
                                           Diagnostics& diagnostics,
                                           unsigned address_bits,
                                           const LayoutQuery& size_of,
                                           const LayoutQuery& align_of,
                                           EvaluationLimits limits,
                                           EvaluationLayout layout) {
    const auto* input = sources.add(path, std::string(source));
    return expand_procedural_macros(sources, *input, diagnostics,
                                    address_bits, size_of, align_of, limits, layout);
}

} // namespace cross
