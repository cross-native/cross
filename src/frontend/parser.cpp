// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <utility>

namespace cross {
namespace {

std::optional<BuiltinType> builtin_kind(std::string_view spelling) {
    static constexpr std::pair<std::string_view, BuiltinType> types[] = {
        {"void", BuiltinType::Void}, {"bool", BuiltinType::Bool},
        {"i8", BuiltinType::I8}, {"u8", BuiltinType::U8},
        {"i16", BuiltinType::I16}, {"u16", BuiltinType::U16},
        {"i32", BuiltinType::I32}, {"u32", BuiltinType::U32},
        {"i64", BuiltinType::I64}, {"u64", BuiltinType::U64},
        {"i128", BuiltinType::I128}, {"u128", BuiltinType::U128},
        {"iptr", BuiltinType::Iptr}, {"uptr", BuiltinType::Uptr},
        {"f32", BuiltinType::F32}, {"f64", BuiltinType::F64},
        {"f80", BuiltinType::F80}, {"f128", BuiltinType::F128},
        {"fptr", BuiltinType::Fptr},
        {"label", BuiltinType::Label},
    };
    for (const auto& [name, kind] : types) if (spelling == name) return kind;
    return std::nullopt;
}

std::optional<std::string> familiar_c_spelling(std::string_view spelling) {
    if (spelling == "int" || spelling == "long" || spelling == "short" ||
        spelling == "signed" || spelling == "unsigned") {
        return "no visible Cross type named '" + std::string(spelling) +
               "'; Cross integers use exact names such as i32, u32, i64, "
               "and u64";
    }
    if (spelling == "char") {
        return "no visible Cross type named 'char'; use i8 or u8 for an "
               "8-bit integer";
    }
    if (spelling == "float") {
        return "no visible Cross type named 'float'; use f32";
    }
    if (spelling == "double") {
        return "no visible Cross type named 'double'; use f64";
    }
    if (spelling == "_Bool") {
        return "no visible Cross type named '_Bool'; use bool";
    }
    if (spelling == "auto") {
        return "Cross has no 'auto' type inference; write the exact type";
    }
    if (spelling == "extern") {
        return "'extern' is not Cross syntax; a declaration without a body "
               "or initializer is already a forward declaration";
    }
    if (spelling == "__attribute__" || spelling == "__declspec") {
        return "vendor attribute syntax is not Cross syntax; use a "
               "[[attribute]] spelling";
    }
    return std::nullopt;
}

std::string join_namespace(std::string_view prefix, std::string_view name) {
    if (prefix.empty()) return std::string(name);
    return std::string(prefix) + "::" + std::string(name);
}

std::optional<std::int64_t> constant_value(const Expr& expression) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return constant_value(*expression.left);
    }
    if (expression.kind == Expr::Kind::Integer) {
        auto text = expression.text;
        for (const auto suffix : {"iptr", "uptr", "i128", "u128", "i64", "u64", "i32", "u32",
                                  "i16", "u16", "i8", "u8"}) {
            const std::string_view suffix_view(suffix);
            if (text.size() > suffix_view.size() && text.ends_with(suffix_view)) {
                text.resize(text.size() - suffix_view.size());
                break;
            }
        }
        text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
        unsigned base = 10;
        std::string_view digits(text);
        if (digits.starts_with("0x") || digits.starts_with("0X")) { base = 16; digits.remove_prefix(2); }
        else if (digits.starts_with("0b") || digits.starts_with("0B")) { base = 2; digits.remove_prefix(2); }
        std::uint64_t value{};
        const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), value,
                                            static_cast<int>(base));
        if (result.ec != std::errc{}) return std::nullopt;
        return static_cast<std::int64_t>(value);
    }
    if (expression.kind == Expr::Kind::Unary) {
        const auto operand = constant_value(*expression.left);
        if (!operand) return std::nullopt;
        if (expression.text == "+") return *operand;
        if (expression.text == "-") return -*operand;
        if (expression.text == "!") return *operand == 0;
        if (expression.text == "~") return ~*operand;
        return std::nullopt;
    }
    if (expression.kind == Expr::Kind::Conditional) {
        const auto condition = constant_value(*expression.left);
        if (!condition) return std::nullopt;
        return constant_value(*(*condition ? expression.right : expression.third));
    }
    if (expression.kind != Expr::Kind::Binary) return std::nullopt;
    const auto left = constant_value(*expression.left);
    const auto right = constant_value(*expression.right);
    if (!left || !right) return std::nullopt;
    if (expression.text == "+") return *left + *right;
    if (expression.text == "-") return *left - *right;
    if (expression.text == "*") return *left * *right;
    if (expression.text == "/") return *right == 0 ? std::nullopt : std::optional(*left / *right);
    if (expression.text == "%") return *right == 0 ? std::nullopt : std::optional(*left % *right);
    if (expression.text == "<<") return *left << *right;
    if (expression.text == ">>") return *left >> *right;
    if (expression.text == "<") return *left < *right;
    if (expression.text == "<=") return *left <= *right;
    if (expression.text == ">") return *left > *right;
    if (expression.text == ">=") return *left >= *right;
    if (expression.text == "==") return *left == *right;
    if (expression.text == "!=") return *left != *right;
    if (expression.text == "&") return *left & *right;
    if (expression.text == "^") return *left ^ *right;
    if (expression.text == "|") return *left | *right;
    if (expression.text == "&&") return *left != 0 && *right != 0;
    if (expression.text == "||") return *left != 0 || *right != 0;
    return std::nullopt;
}

} // namespace

Parser::Parser(std::vector<Token> tokens, Diagnostics& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

const Token& Parser::current(std::size_t lookahead) const {
    const auto position = index_ + lookahead;
    return tokens_[position < tokens_.size() ? position : tokens_.size() - 1];
}

bool Parser::consume(std::string_view spelling) {
    if (!current().is(spelling)) return false;
    ++index_;
    return true;
}

const Token* Parser::consume_kind(TokenKind kind) {
    if (current().kind != kind) return nullptr;
    return &tokens_[index_++];
}

bool Parser::expect(std::string_view spelling, std::string_view context) {
    if (consume(spelling)) return true;
    std::string message = "expected '" + std::string(spelling) + "'";
    if (!context.empty()) message += " " + std::string(context);
    error_here(std::move(message));
    return false;
}

void Parser::error_here(std::string message) {
    diagnostics_.error(current().location, message);
}

void Parser::synchronize_external() {
    while (current().kind != TokenKind::End) {
        if (consume(";")) return;
        if (current().is("}")) return;
        ++index_;
    }
}

std::vector<Attribute> Parser::parse_attributes() {
    std::vector<Attribute> result;
    while (consume("[[")) {
        do {
            if (current().kind == TokenKind::BuiltinName) {
                diagnostics_.error(
                    current().location,
                    "attributes are contextual names; omit the '$::' prefix");
                ++index_;
                while (!current().is("]]" ) && current().kind != TokenKind::End) ++index_;
                break;
            }
            const auto* first = consume_kind(TokenKind::Identifier);
            if (!first) {
                error_here("expected attribute name");
                while (!current().is("]]" ) && current().kind != TokenKind::End) ++index_;
                break;
            }
            std::string name(first->text);
            while (consume("::")) {
                const auto* component = consume_kind(TokenKind::Identifier);
                if (!component) {
                    error_here("expected attribute-name component after '::'");
                    break;
                }
                name += "::";
                name += component->text;
            }
            Attribute attribute{std::move(name), {}, first->location};
            if (consume("(")) {
                if (attribute.name == "generic") {
                    const auto saved_generic_types = active_generic_types_;
                    while (!current().is(")") &&
                           current().kind != TokenKind::End) {
                        const auto start = index_;
                        auto parameter_location = current().location;
                        std::optional<std::string> parameter_name;
                        TypePtr value_type;
                        if (current().kind == TokenKind::Identifier &&
                            (current(1).is(",") || current(1).is(")"))) {
                            parameter_name = std::string(current().text);
                            ++index_;
                            active_generic_types_.push_back(*parameter_name);
                        } else {
                            value_type = parse_type();
                            if (value_type) {
                                value_type = parse_declarator(
                                    std::move(value_type), parameter_name, false,
                                    nullptr, &parameter_location);
                            }
                            if (value_type && !is_integer(value_type) &&
                                value_type->kind != Type::Kind::Pointer &&
                                value_type->kind != Type::Kind::Generic &&
                                !(value_type->kind == Type::Kind::Builtin &&
                                  value_type->builtin == BuiltinType::Label)) {
                                diagnostics_.error(tokens_[start].location,
                                    "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
                            }
                        }
                        if (!parameter_name ||
                            parameter_name->find("::") != std::string::npos) {
                            diagnostics_.error(tokens_[start].location,
                                               "expected an unqualified generic parameter name");
                        } else if (std::any_of(
                                       attribute.generic_parameters.begin(),
                                       attribute.generic_parameters.end(),
                                       [&](const auto& parameter) {
                                           return parameter.name == *parameter_name;
                                       })) {
                            diagnostics_.error(tokens_[start].location,
                                               "duplicate 'generic' parameter '" +
                                                   *parameter_name + "'");
                        } else {
                            attribute.generic_parameters.push_back(
                                {std::move(*parameter_name), std::move(value_type), parameter_location});
                        }
                        std::string spelling;
                        for (auto token = start; token < index_; ++token) {
                            if (!spelling.empty()) spelling += ' ';
                            spelling += tokens_[token].text;
                        }
                        attribute.arguments.push_back(std::move(spelling));
                        if (index_ == start || !consume(",")) break;
                        if (current().is(")")) {
                            error_here("empty 'generic' parameter");
                            break;
                        }
                    }
                    if (attribute.generic_parameters.empty()) {
                        diagnostics_.error(attribute.location,
                                           "'generic' requires at least one parameter");
                    }
                    expect(")", "after generic parameters");
                    active_generic_types_ = saved_generic_types;
                } else if (attribute.name == "aligned") {
                    while (!current().is(")") &&
                           current().kind != TokenKind::End) {
                        const auto start = index_;
                        auto expression = parse_expression();
                        if (index_ == start) {
                            error_here("expected alignment expression");
                            break;
                        }
                        std::string argument;
                        for (auto index = start; index < index_; ++index) {
                            argument += tokens_[index].text;
                        }
                        attribute.arguments.push_back(std::move(argument));
                        if (attribute.arguments.size() == 1) {
                            attribute.expression_argument =
                                std::move(expression);
                        }
                        if (!consume(",")) break;
                    }
                    expect(")", "after alignment expression");
                } else {
                    unsigned depth = 1;
                    std::string argument;
                    while (depth != 0 && current().kind != TokenKind::End) {
                        if (current().is("(") ) { ++depth; argument += current().text; ++index_; continue; }
                        if (current().is(")")) {
                            --depth;
                            if (depth == 0) {
                                if (!argument.empty()) attribute.arguments.push_back(argument);
                                ++index_;
                                break;
                            }
                            argument += current().text; ++index_; continue;
                        }
                        if (depth == 1 && current().is(",")) {
                            attribute.arguments.push_back(argument);
                            argument.clear();
                            ++index_;
                            continue;
                        }
                        argument += current().text;
                        ++index_;
                    }
                }
            }
            result.push_back(std::move(attribute));
        } while (consume(","));
        expect("]]", "to close attribute list");
    }
    return result;
}

void Parser::apply_type_attributes(
    TypePtr& type,
    std::optional<std::pair<std::uint32_t, SourceLocation>>*
        pending_address_space) {
    if (!current().is("[[")) return;
    for (const auto& attribute : parse_attributes()) {
        if (attribute.name == "atomic") {
            if (!attribute.arguments.empty()) {
                diagnostics_.error(attribute.location,
                                   "'atomic' takes no arguments");
            } else if (type->is_atomic) {
                diagnostics_.error(attribute.location,
                                   "duplicate 'atomic' type qualifier");
            } else {
                type->is_atomic = true;
            }
            continue;
        }
        if (attribute.name == "address_space") {
            if (attribute.arguments.size() != 1) {
                diagnostics_.error(attribute.location,
                                   "address_space requires one target registry number");
                continue;
            }
            auto digits = attribute.arguments.front();
            digits.erase(std::remove(digits.begin(), digits.end(), '_'),
                         digits.end());
            int base = 10;
            if (digits.starts_with("0x") || digits.starts_with("0X")) {
                digits.erase(0, 2);
                base = 16;
            } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
                digits.erase(0, 2);
                base = 2;
            }
            std::uint32_t number{};
            const auto parsed = std::from_chars(
                digits.data(), digits.data() + digits.size(), number, base);
            if (digits.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != digits.data() + digits.size()) {
                diagnostics_.error(attribute.location,
                                   "address_space requires a nonnegative target registry number");
                continue;
            }
            if (pending_address_space) {
                if (*pending_address_space) {
                    diagnostics_.error(attribute.location,
                                       "duplicate address_space type qualifier");
                } else {
                    *pending_address_space =
                        std::pair{number, attribute.location};
                }
            } else if (type->kind != Type::Kind::Pointer) {
                diagnostics_.error(attribute.location,
                                   "address_space requires a pointer type");
            } else if (type->address_space_location.valid()) {
                diagnostics_.error(attribute.location,
                                   "duplicate address_space type qualifier");
            } else {
                type->address_space = number;
                type->address_space_location = attribute.location;
            }
            continue;
        }
        diagnostics_.error(attribute.location,
                           "attribute '" + attribute.name +
                               "' is not valid as a type qualifier here");
    }
}

std::optional<std::string> Parser::parse_qualified_name() {
    const auto* first = consume_kind(TokenKind::Identifier);
    if (!first) return std::nullopt;
    std::string name(first->text);
    while (current().is("::") && current(1).kind == TokenKind::Identifier) {
        consume("::");
        const auto* component = consume_kind(TokenKind::Identifier);
        if (!component) {
            error_here("expected identifier after '::'");
            break;
        }
        name += "::";
        name += component->text;
    }
    return name;
}

std::string Parser::peek_qualified_name() const {
    if (current().kind != TokenKind::Identifier) return {};
    std::string result(current().text);
    auto position = index_ + 1;
    while (position + 1 < tokens_.size() &&
           tokens_[position].is("::") &&
           tokens_[position + 1].kind == TokenKind::Identifier) {
        result += "::";
        result += tokens_[position + 1].text;
        position += 2;
    }
    return result;
}

TypePtr Parser::resolve_type_alias(std::string_view name) const {
    const auto find = [&](std::string_view candidate) -> TypePtr {
        const auto found = type_aliases_.find(std::string(candidate));
        return found == type_aliases_.end() ? TypePtr{} : found->second;
    };
    if (const auto exact = find(name)) return exact;
    if (name.find("::") != std::string_view::npos) return {};
    auto current_namespace = active_namespace_;
    while (!current_namespace.empty()) {
        if (const auto local = find(join_namespace(current_namespace, name))) {
            return local;
        }
        const auto separator = current_namespace.rfind("::");
        if (separator == std::string::npos) break;
        current_namespace.resize(separator);
    }
    for (const auto& imported : active_imports_) {
        if (const auto type = find(join_namespace(imported, name))) return type;
    }
    return {};
}

bool Parser::type_start() const {
    const auto& token = current();
    return token.is("const") || token.is("volatile") ||
           token.is("$::meta::tokens") ||
           token.is("restrict") || token.is("enum") ||
           token.is("struct") || token.is("union") ||
           builtin_kind(token.text).has_value() ||
           std::find(active_generic_types_.begin(), active_generic_types_.end(),
                     token.text) != active_generic_types_.end() ||
           resolve_type_alias(peek_qualified_name()) != nullptr;
}

TypePtr Parser::parse_type() {
    bool is_const = false;
    bool is_volatile = false;
    bool is_restrict = false;
    std::optional<SourceLocation> restrict_location;
    while (current().is("const") || current().is("volatile") ||
           current().is("restrict")) {
        if (consume("const")) is_const = true;
        else if (consume("volatile")) is_volatile = true;
        else {
            restrict_location = current().location;
            consume("restrict");
            is_restrict = true;
        }
    }
    TypePtr type;
    if (current().is("$::meta::tokens")) {
        if (!parsing_procedural_body_)
            error_here("$::meta::tokens is only available in translation-time macro bodies");
        ++index_;
        type = tokens_type();
        type->is_const = is_const;
        type->is_volatile = is_volatile;
    } else if (current().is("struct") || current().is("union")) {
        const bool is_union = consume("union");
        if (!is_union) consume("struct");
        const auto name = parse_qualified_name();
        if (!name) {
            error_here("expected record name after '" +
                       std::string(is_union ? "union" : "struct") + "'");
            return {};
        }
        auto canonical = *name;
        auto found = record_types_.end();
        if (name->find("::") != std::string::npos) {
            found = record_types_.find(canonical);
        } else {
            auto current_namespace = active_namespace_;
            while (!current_namespace.empty()) {
                canonical = join_namespace(current_namespace, *name);
                found = record_types_.find(canonical);
                if (found != record_types_.end()) break;
                const auto separator = current_namespace.rfind("::");
                if (separator == std::string::npos) break;
                current_namespace.resize(separator);
            }
            for (const auto& imported : active_imports_) {
                if (found != record_types_.end()) break;
                canonical = join_namespace(imported, *name);
                found = record_types_.find(canonical);
            }
            if (found == record_types_.end()) {
                canonical = *name;
                found = record_types_.find(canonical);
            }
        }
        if (found == record_types_.end()) {
            canonical = name->find("::") == std::string::npos &&
                                !active_namespace_.empty()
                            ? join_namespace(active_namespace_, *name)
                            : *name;
            record_types_.emplace(canonical, RecordTag{is_union, false});
        } else if (found->second.is_union != is_union) {
            diagnostics_.error(
                current().location,
                "record tag '" + *name +
                    "' was previously declared with the other record kind");
        }
        type = record_type(canonical, is_union, is_const, is_volatile);
    } else if (consume("enum")) {
        const auto name = parse_qualified_name();
        if (!name) {
            error_here("expected enumeration name after 'enum'");
            return {};
        }
        auto canonical = *name;
        auto found = enum_types_.find(canonical);
        if (found == enum_types_.end() &&
            canonical.find("::") == std::string::npos &&
            !active_namespace_.empty()) {
            canonical = join_namespace(active_namespace_, canonical);
            found = enum_types_.find(canonical);
        }
        if (found == enum_types_.end() &&
            name->find("::") == std::string::npos) {
            for (const auto& imported : active_imports_) {
                canonical = join_namespace(imported, *name);
                found = enum_types_.find(canonical);
                if (found != enum_types_.end()) break;
            }
        }
        if (found == enum_types_.end()) {
            diagnostics_.error(current().location,
                               "unknown enumeration type '" + *name + "'");
            type = enum_type(*name, BuiltinType::I32, is_const, is_volatile);
        } else {
            type = enum_type(canonical, found->second, is_const, is_volatile);
        }
    }
    const auto kind = type ? std::optional<BuiltinType>{}
                           : builtin_kind(current().text);
    const auto generic = std::find(active_generic_types_.begin(),
                                   active_generic_types_.end(), current().text);
    const auto alias_name = type ? std::string{} : peek_qualified_name();
    const auto alias = alias_name.empty() ? TypePtr{}
                                          : resolve_type_alias(alias_name);
    if (!type && !kind && generic == active_generic_types_.end() && !alias) {
        if (const auto message = familiar_c_spelling(current().text)) {
            error_here(*message);
            ++index_;
            return builtin_type(BuiltinType::I32, is_const, is_volatile);
        }
        error_here("expected Cross type");
        return {};
    }
    if (!type) {
        if (alias) {
            (void)parse_qualified_name();
            type = std::make_shared<Type>(*alias);
            type->is_const = type->is_const || is_const;
            type->is_volatile = type->is_volatile || is_volatile;
        } else {
            const auto spelling = std::string(current().text);
            ++index_;
            type = kind ? builtin_type(*kind, is_const, is_volatile)
                        : generic_type(spelling, is_const, is_volatile);
        }
    }
    type->is_restrict = type->is_restrict || is_restrict;
    if (type->is_restrict && type->kind != Type::Kind::Pointer) {
        diagnostics_.error(
            restrict_location.value_or(current().location),
            "restrict qualifier requires a pointer type");
    }
    std::optional<std::pair<std::uint32_t, SourceLocation>>
        pending_address_space;
    apply_type_attributes(type, &pending_address_space);
    while (consume("*")) {
        bool pointer_const = false;
        bool pointer_volatile = false;
        bool pointer_restrict = false;
        while (current().is("const") || current().is("volatile") ||
               current().is("restrict")) {
            if (consume("const")) pointer_const = true;
            else if (consume("volatile")) pointer_volatile = true;
            else {
                consume("restrict");
                pointer_restrict = true;
            }
        }
        type = pointer_type(type, pointer_const, pointer_volatile);
        type->is_restrict = pointer_restrict;
        if (pending_address_space) {
            type->address_space = pending_address_space->first;
            type->address_space_location = pending_address_space->second;
            pending_address_space.reset();
        }
        apply_type_attributes(type);
    }
    if (pending_address_space) {
        if (current().is("(") &&
            (current(1).is("*") || current(1).is("("))) {
            type->pending_address_space = pending_address_space;
        } else if (type->kind != Type::Kind::Pointer) {
            diagnostics_.error(pending_address_space->second,
                               "address_space requires a pointer type");
        } else if (type->address_space_location.valid()) {
            diagnostics_.error(pending_address_space->second,
                               "duplicate address_space type qualifier");
        } else {
            type->address_space = pending_address_space->first;
            type->address_space_location = pending_address_space->second;
        }
    }
    return type;
}

std::vector<std::string> Parser::preview_angle_generic_types() const {
    for (auto cursor = index_; cursor < tokens_.size(); ++cursor) {
        if (tokens_[cursor].is(";") || tokens_[cursor].is("{") ||
            tokens_[cursor].is("=")) break;
        if (!tokens_[cursor].is("<") || cursor == 0 ||
            tokens_[cursor - 1].kind != TokenKind::Identifier) continue;
        unsigned depth = 1;
        auto close = cursor + 1;
        for (; close < tokens_.size(); ++close) {
            if (tokens_[close].is("<")) ++depth;
            else if (tokens_[close].is(">")) --depth;
            else if (tokens_[close].is(">>")) {
                if (depth < 2) break;
                depth -= 2;
            }
            if (depth == 0) break;
        }
        if (depth != 0 || close + 1 >= tokens_.size() ||
            !tokens_[close + 1].is("(")) continue;
        std::vector<std::string> result;
        auto begin = cursor + 1;
        for (auto index = begin; index <= close; ++index) {
            if (index != close && !tokens_[index].is(",")) continue;
            if (index == begin + 1 &&
                tokens_[begin].kind == TokenKind::Identifier)
                result.emplace_back(tokens_[begin].text);
            begin = index + 1;
        }
        return result;
    }
    return {};
}

std::vector<FunctionDecl::GenericParameter>
Parser::parse_angle_generic_parameters() {
    std::vector<FunctionDecl::GenericParameter> result;
    consume("<");
    if (consume(">")) {
        error_here("a generic parameter list cannot be empty");
        return result;
    }
    for (;;) {
        const auto location = current().location;
        std::optional<std::string> name;
        TypePtr value_type;
        if (current().kind == TokenKind::Identifier &&
            (current(1).is(",") || current(1).is(">"))) {
            name = std::string(current().text);
            ++index_;
        } else {
            value_type = parse_type();
            if (value_type)
                value_type = parse_declarator(std::move(value_type), name);
            if (value_type && !is_integer(value_type) &&
                value_type->kind != Type::Kind::Pointer &&
                !(value_type->kind == Type::Kind::Builtin &&
                  value_type->builtin == BuiltinType::Label))
                diagnostics_.error(location,
                    "generic value parameter requires an integer, enumeration, bool, label, or pointer type");
        }
        if (!name || name->find("::") != std::string::npos) {
            diagnostics_.error(location,
                               "expected an unqualified generic parameter name");
        } else if (std::any_of(result.begin(), result.end(),
                               [&](const auto& parameter) {
                                   return parameter.name == *name;
                               })) {
            diagnostics_.error(location,
                               "duplicate generic parameter '" + *name + "'");
        } else {
            if (!value_type) active_generic_types_.push_back(*name);
            result.push_back({std::move(*name), std::move(value_type), location});
        }
        if (!consume(",")) break;
        if (current().is(">")) {
            error_here("a generic parameter list cannot have a trailing comma");
            break;
        }
    }
    expect(">", "after generic parameters");
    return result;
}

bool Parser::known_generic_name(std::string_view name) const {
    if (name.find("::") == std::string_view::npos)
        for (auto scope = local_scopes_.rbegin();
             scope != local_scopes_.rend(); ++scope)
            if (scope->contains(std::string(name))) return false;
    if (known_generic_functions_.contains(std::string(name))) return true;
    if (name.find("::") != std::string_view::npos) return false;
    auto name_space = active_namespace_;
    while (!name_space.empty()) {
        if (known_generic_functions_.contains(join_namespace(name_space, name)))
            return true;
        const auto separator = name_space.rfind("::");
        if (separator == std::string::npos) break;
        name_space.resize(separator);
    }
    for (const auto& imported : active_imports_)
        if (known_generic_functions_.contains(join_namespace(imported, name)))
            return true;
    return false;
}

bool Parser::consume_generic_close() {
    if (consume(">")) return true;
    if (!current().is(">>")) return false;
    auto remainder = current();
    remainder.text = ">";
    ++remainder.location.offset;
    ++remainder.location.column;
    tokens_[index_].text = ">";
    tokens_.insert(tokens_.begin() + static_cast<std::ptrdiff_t>(index_) + 1,
                   remainder);
    ++index_;
    return true;
}

TypePtr Parser::parse_declarator(TypePtr base, std::optional<std::string>& name,
                                 bool parameter,
                                 std::unique_ptr<Expr>* dynamic_outer_bound,
                                 SourceLocation* name_location,
                                 std::vector<FunctionDecl::GenericParameter>*
                                     angle_parameters) {
    while (consume("*")) {
        bool is_const = false;
        bool is_volatile = false;
        bool is_restrict = false;
        while (current().is("const") || current().is("volatile") ||
               current().is("restrict")) {
            if (consume("const"))
                is_const = true;
            else if (consume("volatile")) {
                is_volatile = true;
            } else {
                consume("restrict");
                is_restrict = true;
            }
        }
        base = pointer_type(std::move(base), is_const, is_volatile);
        base->is_restrict = is_restrict;
        apply_type_attributes(base);
    }
    TypePtr nested;
    TypePtr hole;
    if (current().is("(") && (current(1).is("*") || current(1).is("("))) {
        consume("(");
        hole = std::make_shared<Type>();
        nested = parse_declarator(hole, name, parameter, nullptr,
                                  name_location, angle_parameters);
        expect(")", "after parenthesized declarator");
    } else {
        const auto location = current().location;
        name = parse_qualified_name();
        if (name && name_location) *name_location = location;
        if (name && angle_parameters && current().is("<"))
            *angle_parameters = parse_angle_generic_parameters();
    }
    if (current().is("[")) {
        base = parse_array_suffix(std::move(base), parameter, dynamic_outer_bound);
    }
    if (consume("(")) {
        std::vector<ParameterDecl> parameters;
        bool variadic = false;
        if (!consume(")")) {
            if (current().is("void") && current(1).is(")")) {
                ++index_;
            } else {
                unsigned ordinal = 0;
                for (;;) {
                    if (consume("...")) {
                        variadic = true;
                        break;
                    }
                    const auto before = index_;
                    parameters.push_back(parse_parameter(ordinal++));
                    if (index_ == before || !consume(",")) break;
                }
            }
            expect(")", "after function parameters");
        }
        base = function_type(std::move(base), std::move(parameters), variadic);
        if (consume("->")) {
            const auto* location_token = consume_kind(TokenKind::String);
            if (!location_token) {
                error_here("expected result location string after '->'");
            } else {
                base->function->result_location =
                    decode_string_literal(location_token->text);
                if (!base->function->result_location)
                    diagnostics_.error(location_token->location,
                                       "invalid result location string");
            }
        }
        // In a grouped declarator the attributes follow this function
        // suffix, even if another callable component surrounds it.
        if (nested) {
            auto suffix_attributes = parse_attributes();
            apply_callable_attributes(base, suffix_attributes);
            for (const auto& attribute : suffix_attributes) {
                if (attribute.name != "abi" && attribute.name != "clobber" &&
                    attribute.name != "stack_cleanup")
                    diagnostics_.error(attribute.location,
                                       "function-only attribute cannot qualify a nested callable type");
            }
        }
    }
    if (nested) {
        // The inner declarator binds first. Fill its unique placeholder only
        // after the outer suffix has formed the result/element type.
        TypePtr* attributed_base = &base;
        while (*attributed_base) {
            if ((*attributed_base)->kind == Type::Kind::Array) {
                attributed_base = &(*attributed_base)->element;
            } else if ((*attributed_base)->kind == Type::Kind::Function &&
                       (*attributed_base)->function) {
                attributed_base = &(*attributed_base)->function->result;
            } else {
                break;
            }
        }
        auto pending_address_space = *attributed_base
            ? (*attributed_base)->pending_address_space
            : std::optional<std::pair<std::uint32_t, SourceLocation>>{};
        if (*attributed_base) {
            (*attributed_base)->pending_address_space.reset();
        }
        const auto fill = [&](auto&& self, TypePtr& type) -> void {
            if (type == hole) {
                type = base;
                return;
            }
            if (!type) return;
            if (type->kind == Type::Kind::Pointer) {
                if (type->pointee == hole && pending_address_space) {
                    if (type->address_space_location.valid()) {
                        diagnostics_.error(
                            pending_address_space->second,
                            "duplicate address_space type qualifier");
                    } else {
                        type->address_space = pending_address_space->first;
                        type->address_space_location =
                            pending_address_space->second;
                    }
                    pending_address_space.reset();
                }
                self(self, type->pointee);
            } else if (type->kind == Type::Kind::Array)
                self(self, type->element);
            else if (type->kind == Type::Kind::Function && type->function)
                self(self, type->function->result);
        };
        fill(fill, nested);
        if (pending_address_space) {
            diagnostics_.error(pending_address_space->second,
                               "address_space requires a pointer type");
        }
        base = std::move(nested);
    }
    if (parameter && base) {
        if (base->kind == Type::Kind::Array)
            base = pointer_type(base->element);
        else if (base->kind == Type::Kind::Function)
            base = pointer_type(base);
    }
    return base;
}

void Parser::apply_callable_attributes(
    TypePtr& type, const std::vector<Attribute>& attributes) {
    for (const auto& attribute : attributes) {
        if (attribute.name != "abi" && attribute.name != "clobber" &&
            attribute.name != "stack_cleanup") continue;
        if (type && type->kind != Type::Kind::Function) {
            const auto count = [&](const auto& self, const TypePtr& item)
                                   -> unsigned {
                if (!item) return 0;
                if (item->kind == Type::Kind::Pointer)
                    return self(self, item->pointee);
                if (item->kind == Type::Kind::Array)
                    return self(self, item->element);
                if (item->kind == Type::Kind::Function && item->function)
                    return 1 + self(self, item->function->result);
                return 0;
            };
            if (count(count, type) > 1) {
                diagnostics_.error(
                    attribute.location,
                    "leading callable attribute is ambiguous between nested function types");
                continue;
            }
        }
        TypePtr* node = &type;
        while (*node && ((*node)->kind == Type::Kind::Pointer ||
                         (*node)->kind == Type::Kind::Array)) {
            *node = std::make_shared<Type>(**node);
            node = (*node)->kind == Type::Kind::Pointer ? &(*node)->pointee
                                                        : &(*node)->element;
        }
        if (!*node || (*node)->kind != Type::Kind::Function ||
            !(*node)->function) {
            diagnostics_.error(
                attribute.location,
                "callable attribute requires a function or function-pointer type");
            continue;
        }
        *node = std::make_shared<Type>(**node);
        (*node)->function = std::make_shared<FunctionType>(*(*node)->function);
        auto& signature = *(*node)->function;
        if (attribute.name == "clobber") {
            if (attribute.arguments.empty()) {
                diagnostics_.error(attribute.location,
                                   "'clobber' requires string arguments");
            }
            for (const auto& argument : attribute.arguments) {
                const auto resource = decode_string_literal(argument);
                if (!resource || resource->empty())
                    diagnostics_.error(attribute.location,
                                       "clobber arguments must be nonempty strings");
                else
                    signature.clobbers.push_back(*resource);
            }
            continue;
        }
        const auto spelling = attribute.arguments.size() == 1
                                  ? decode_string_literal(attribute.arguments.front())
                                  : std::nullopt;
        if (!spelling || spelling->empty()) {
            diagnostics_.error(attribute.location,
                               "'" + attribute.name +
                                   "' requires one nonempty string");
            continue;
        }
        if (attribute.name == "abi") signature.abi = *spelling;
        else signature.stack_cleanup = *spelling;
    }
}

TypePtr Parser::parse_array_suffix(
    TypePtr element, bool parameter,
    std::unique_ptr<Expr>* dynamic_outer_bound) {
    std::vector<std::uint32_t> bounds;
    while (consume("[")) {
        if (consume("]")) {
            if (parameter) {
                diagnostics_.error(
                    current().location,
                    "an array parameter requires a positive fixed bound in the bootstrap compiler");
                bounds.push_back(1);
            } else {
                bounds.push_back(0);
            }
            continue;
        }
        const auto location = current().location;
        auto expression = parse_expression();
        expect("]", "after array bound");
        const auto value = expression ? constant_value(*expression)
                                      : std::nullopt;
        if (!value && dynamic_outer_bound && bounds.empty() &&
            !*dynamic_outer_bound) {
            *dynamic_outer_bound = std::move(expression);
            bounds.push_back(0);
        } else if (!value || *value <= 0 ||
            static_cast<std::uint64_t>(*value) >
                std::numeric_limits<std::uint32_t>::max()) {
            diagnostics_.error(
                location,
                dynamic_outer_bound
                    ? "only the outermost array bound may be a runtime value"
                    : "fixed array bound must be a positive integer translation-time value");
            bounds.push_back(1);
        } else {
            bounds.push_back(static_cast<std::uint32_t>(*value));
        }
    }
    for (auto bound = bounds.rbegin(); bound != bounds.rend(); ++bound) {
        element = array_type(std::move(element), *bound);
    }
    if (parameter && element && element->kind == Type::Kind::Array) {
        element = pointer_type(element->element);
    }
    return element;
}

std::vector<FunctionDecl::GenericParameter> Parser::generic_parameters(
    const std::vector<Attribute>& attributes) {
    std::vector<FunctionDecl::GenericParameter> result;
    bool seen = false;
    for (const auto& attribute : attributes) {
        if (attribute.name != "generic") continue;
        if (seen) {
            diagnostics_.error(attribute.location,
                               "a function has at most one 'generic' attribute");
            continue;
        }
        seen = true;
        result = attribute.generic_parameters;
    }
    return result;
}

Program Parser::parse() {
    Program program;
    while (current().kind != TokenKind::End) {
        const auto before = index_;
        parse_external(program, {});
        // A top-level stray closing brace is a synchronization boundary for a
        // nested namespace, but it must not stall the outermost parse loop.
        if (index_ == before && current().kind != TokenKind::End) ++index_;
    }
    program.static_assertions = std::move(static_assertions_);
    return program;
}

void Parser::parse_typedef(const std::string& name_space,
                           std::vector<Attribute> attributes) {
    const auto location = current().location;
    if (!type_start()) {
        error_here("expected aliased type after 'typedef'");
        synchronize_external();
        return;
    }
    auto type = parse_type();
    std::optional<std::string> name;
    type = parse_declarator(std::move(type), name);
    if (!type || !name) {
        if (!name) error_here("expected typedef name");
        synchronize_external();
        return;
    }
    *name = join_namespace(name_space, *name);
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    expect(";", "after typedef declaration");
    apply_callable_attributes(type, attributes);

    const Attribute* vector_attribute = nullptr;
    for (const auto& attribute : attributes) {
        if (attribute.name != "vector_size" &&
            attribute.name != "ext_vector_type" &&
            attribute.name != "scalable_vector") {
            continue;
        }
        if (vector_attribute) {
            diagnostics_.error(
                attribute.location,
                "a typedef has at most one vector type attribute");
            continue;
        }
        vector_attribute = &attribute;
    }
    if (vector_attribute) {
        if (type->kind == Type::Kind::Vector ||
            (!is_integer(type) && !is_floating(type)) ||
            type->is_atomic ||
            type->builtin == BuiltinType::Bool ||
            type->builtin == BuiltinType::F80 ||
            type->builtin == BuiltinType::F128 ||
            type->builtin == BuiltinType::Fptr) {
            diagnostics_.error(
                vector_attribute->location,
                "vector element type must be a supported integer, f32, or f64 scalar");
        } else if (vector_attribute->arguments.size() != 1) {
            diagnostics_.error(
                vector_attribute->location,
                "'" + vector_attribute->name +
                    "' requires one positive integer argument");
        } else {
            const auto& text = vector_attribute->arguments.front();
            std::uint64_t amount{};
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), amount);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != text.data() + text.size() || amount == 0) {
                diagnostics_.error(
                    vector_attribute->location,
                    "'" + vector_attribute->name +
                        "' requires one positive integer argument");
            } else {
                const auto element_bits = type_bits(type);
                std::uint64_t lanes = amount;
                if (vector_attribute->name == "vector_size") {
                    const auto total_bits = amount * 8U;
                    if (element_bits == 0 || total_bits % element_bits != 0) {
                        diagnostics_.error(
                            vector_attribute->location,
                            "vector size must be a multiple of the element size");
                        return;
                    } else {
                        lanes = total_bits / element_bits;
                    }
                }
                if (lanes == 0 ||
                    lanes > std::numeric_limits<std::uint32_t>::max()) {
                    diagnostics_.error(vector_attribute->location,
                                       "vector lane count is out of range");
                } else {
                    type = vector_type(
                        type, static_cast<std::uint32_t>(lanes),
                        vector_attribute->name == "scalable_vector");
                }
            }
        }
    }

    const auto found = type_aliases_.find(*name);
    if (found != type_aliases_.end() && !same_type(found->second, type)) {
        diagnostics_.error(location,
                           "typedef '" + *name +
                               "' redeclared with a different type");
        return;
    }
    type_aliases_[*name] = std::move(type);
}

void Parser::parse_external(Program& program, const std::string& name_space) {
    struct NamespaceRestore {
        std::string& value;
        std::string previous;
        ~NamespaceRestore() { value = std::move(previous); }
    } restore{active_namespace_, active_namespace_};
    active_namespace_ = name_space;
    auto attributes = parse_attributes();
    if (current().is("$::static_assert")) {
        if (!attributes.empty()) error_here("attributes are not valid on $::static_assert");
        (void)parse_static_assertion();
        return;
    }
    if (consume("namespace")) {
        auto nested = parse_qualified_name();
        if (!nested || !expect("{")) { synchronize_external(); return; }
        const auto full = join_namespace(name_space, *nested);
        const auto saved_imports = active_imports_;
        const auto saved_scope_imports = current_scope_imports_;
        current_scope_imports_ = 0;
        while (!current().is("}") && current().kind != TokenKind::End) parse_external(program, full);
        expect("}");
        active_imports_ = saved_imports;
        current_scope_imports_ = saved_scope_imports;
        return;
    }
    if (consume("using")) {
        if (auto imported = parse_qualified_name()) {
            active_imports_.insert(active_imports_.begin() + static_cast<std::ptrdiff_t>(current_scope_imports_), std::move(*imported));
            ++current_scope_imports_;
        }
        else error_here("expected namespace name after 'using'");
        expect(";");
        return;
    }
    if (current().is("enum") &&
        current(1).kind == TokenKind::Identifier &&
        (current(2).is("{") || current(2).is("[[") ||
         current(2).is(";"))) {
        parse_enum_declaration(program, name_space, std::move(attributes));
        return;
    }
    if ((current().is("struct") || current().is("union")) &&
        current(1).kind == TokenKind::Identifier &&
        (current(2).is("{") || current(2).is("[[") ||
         current(2).is(";"))) {
        parse_record_declaration(program, name_space, std::move(attributes));
        return;
    }
    if (consume("typedef")) {
        parse_typedef(name_space, std::move(attributes));
        return;
    }

    Linkage linkage = Linkage::Group;
    bool linkage_seen = false;
    bool inline_hint = false;
    while (current().is("global") || current().is("static") || current().is("inline")) {
        if (consume("inline")) { inline_hint = true; continue; }
        Linkage selected;
        if (consume("global")) selected = Linkage::Global;
        else { consume("static"); selected = Linkage::Static; }
        if (linkage_seen && linkage != selected) error_here("declaration cannot be both 'global' and 'static'");
        linkage = selected;
        linkage_seen = true;
    }

    if (linkage == Linkage::Global && current().is("label") &&
        current(1).kind == TokenKind::Identifier && current(2).is("::")) {
        if (inline_hint) {
            diagnostics_.error(current().location,
                               "'inline' is valid only on a function");
        }
        parse_global_label_declaration(program, name_space,
                                       std::move(attributes));
        return;
    }

    const auto parameters = generic_parameters(attributes);
    const auto saved_generic_types = active_generic_types_;
    active_generic_types_.clear();
    for (const auto& parameter : parameters) {
        if (!parameter.value_type) active_generic_types_.push_back(parameter.name);
    }
    for (const auto& name : preview_angle_generic_types()) {
        if (std::find(active_generic_types_.begin(), active_generic_types_.end(),
                      name) == active_generic_types_.end())
            active_generic_types_.push_back(name);
    }
    if (!type_start()) {
        if (const auto message = familiar_c_spelling(current().text)) {
            error_here(*message);
        } else {
            error_here("expected declaration");
        }
        synchronize_external();
        active_generic_types_ = saved_generic_types;
        return;
    }
    const auto location = current().location;
    auto type = parse_type();
    std::optional<std::string> name;
    std::vector<FunctionDecl::GenericParameter> angle_parameters;
    type = parse_declarator(std::move(type), name, false, nullptr, nullptr,
                            &angle_parameters);
    if (!type || !name) {
        if (!name) error_here("expected declaration name");
        synchronize_external();
        active_generic_types_ = saved_generic_types;
        return;
    }
    *name = join_namespace(name_space, *name);
    if (type->kind == Type::Kind::Function && type->function) {
        if (!angle_parameters.empty() || !parameters.empty())
            known_generic_functions_.insert(*name);
        auto signature = type->function;
        auto result_type = signature->result;
        auto function = parse_function(
            location, std::move(*name), name_space, std::move(result_type),
            linkage, inline_hint, std::move(attributes), std::move(signature),
            std::move(angle_parameters));
        if (function) program.functions.push_back(std::move(function));
    } else {
        if (!angle_parameters.empty())
            diagnostics_.error(location,
                               "angle generic parameters require a direct function declaration");
        apply_callable_attributes(type, attributes);
        if (inline_hint)
            diagnostics_.error(location,
                               "'inline' is valid only on a function");
        auto object = parse_object(location, std::move(*name), std::move(type),
                                   linkage, std::move(attributes));
        if (object) program.objects.push_back(std::move(object));
    }
    active_generic_types_ = saved_generic_types;
}

void Parser::parse_global_label_declaration(
    Program& program, const std::string& name_space,
    std::vector<Attribute> attributes) {
    const auto location = current().location;
    consume("label");
    auto name = parse_qualified_name();
    if (!name || name->find("::") == std::string::npos) {
        diagnostics_.error(location,
                           "a global label declaration requires a qualified label name");
        synchronize_external();
        return;
    }
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    expect(";", "after global label declaration");
    program.global_labels.push_back(
        {location, join_namespace(name_space, *name), std::move(attributes)});
}

void Parser::parse_enum_declaration(Program& program,
                                    const std::string& name_space,
                                    std::vector<Attribute> attributes) {
    const auto location = current().location;
    consume("enum");
    auto name = parse_qualified_name();
    if (!name) {
        error_here("expected enumeration name");
        synchronize_external();
        return;
    }
    *name = join_namespace(name_space, *name);
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));

    BuiltinType underlying = BuiltinType::I32;
    bool underlying_seen = false;
    for (const auto& attribute : attributes) {
        if (attribute.name != "underlying") continue;
        if (underlying_seen) {
            diagnostics_.error(attribute.location,
                               "enumeration has more than one 'underlying' attribute");
            continue;
        }
        underlying_seen = true;
        if (attribute.arguments.size() != 1) {
            diagnostics_.error(attribute.location,
                               "'underlying' requires one integer type");
            continue;
        }
        auto spelling = attribute.arguments.front();
        spelling.erase(std::remove_if(
            spelling.begin(), spelling.end(),
            [](unsigned char ch) { return std::isspace(ch) != 0; }),
            spelling.end());
        const auto kind = builtin_kind(spelling);
        if (!kind || *kind == BuiltinType::Bool ||
            !is_integer(builtin_type(*kind))) {
            diagnostics_.error(attribute.location,
                               "'underlying' requires a non-bool integer type");
            continue;
        }
        underlying = *kind;
    }

    EnumDecl declaration;
    declaration.location = location;
    declaration.name = *name;
    declaration.underlying = underlying;
    declaration.attributes = std::move(attributes);
    if (consume("{")) {
        while (!current().is("}") && current().kind != TokenKind::End) {
            const auto* token = consume_kind(TokenKind::Identifier);
            if (!token) {
                error_here("expected enumerator name");
                while (!current().is(",") && !current().is("}") &&
                       current().kind != TokenKind::End) {
                    ++index_;
                }
            } else {
                EnumDecl::Enumerator enumerator;
                enumerator.location = token->location;
                enumerator.name = join_namespace(name_space, token->text);
                if (consume("=")) enumerator.initializer = parse_assignment();
                declaration.enumerators.push_back(std::move(enumerator));
            }
            if (!consume(",")) break;
        }
        expect("}", "after enumeration definition");
    }
    expect(";", "after enumeration declaration");

    const auto found = enum_types_.find(*name);
    if (found != enum_types_.end() && found->second != underlying) {
        diagnostics_.error(location,
                           "enumeration '" + *name +
                               "' redeclared with a different underlying type");
    } else {
        enum_types_[*name] = underlying;
    }
    program.enumerations.push_back(std::move(declaration));
}

void Parser::parse_record_declaration(
    Program& program, const std::string& name_space,
    std::vector<Attribute> attributes) {
    const auto location = current().location;
    const bool is_union = consume("union");
    if (!is_union) consume("struct");
    auto name = parse_qualified_name();
    if (!name) {
        error_here("expected record name");
        synchronize_external();
        return;
    }
    *name = join_namespace(name_space, *name);
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));

    auto [tag, inserted] = record_types_.emplace(
        *name, RecordTag{is_union, false});
    if (!inserted && tag->second.is_union != is_union) {
        diagnostics_.error(
            location, "record tag '" + *name +
                          "' was previously declared with the other record kind");
    }

    RecordDecl declaration;
    declaration.location = location;
    declaration.name = *name;
    declaration.is_union = is_union;
    declaration.attributes = std::move(attributes);
    if (consume("{")) {
        declaration.complete = true;
        if (!inserted && tag->second.complete) {
            diagnostics_.error(location,
                               "duplicate definition of record '" + *name + "'");
        }
        tag->second.complete = true;
        while (!current().is("}") && current().kind != TokenKind::End) {
            auto member_attributes = parse_attributes();
            if (!type_start()) {
                error_here("expected record member declaration");
                while (!current().is(";") && !current().is("}") &&
                       current().kind != TokenKind::End) {
                    ++index_;
                }
                consume(";");
                continue;
            }
            const auto member_location = current().location;
            auto base_type = parse_type();
            bool parsed_member = false;
            do {
                std::optional<std::string> member_name;
                auto member_type = parse_declarator(base_type, member_name);
                auto item_attributes = parse_attributes();
                item_attributes.insert(
                    item_attributes.begin(), member_attributes.begin(),
                    member_attributes.end());
                std::unique_ptr<Expr> bit_width;
                if (consume(":")) {
                    bit_width = parse_expression();
                    auto trailing_attributes = parse_attributes();
                    item_attributes.insert(
                        item_attributes.end(),
                        std::make_move_iterator(trailing_attributes.begin()),
                        std::make_move_iterator(trailing_attributes.end()));
                } else if (!member_name) {
                    error_here("expected record member name");
                    break;
                }
                declaration.members.push_back(
                    {member_location, member_name.value_or(std::string{}),
                     std::move(member_type), std::move(bit_width),
                     std::move(item_attributes)});
                parsed_member = true;
            } while (consume(","));
            if (!parsed_member) {
                while (!current().is(";") && !current().is("}") &&
                       current().kind != TokenKind::End) {
                    ++index_;
                }
            }
            expect(";", "after record member declaration");
        }
        expect("}", "after record definition");
    }
    expect(";", "after record declaration");
    program.records.push_back(std::move(declaration));
}

bool Parser::parse_static_assertion() {
    const auto location = current().location;
    consume("$::static_assert");
    expect("(");
    auto condition = parse_expression();
    expect(",");
    const auto* message_token = consume_kind(TokenKind::String);
    if (!message_token) error_here("expected diagnostic string in $::static_assert");
    expect(")");
    expect(";");
    const auto message = message_token
                             ? decode_string_literal(message_token->text)
                                   .value_or("static assertion failed")
                             : std::string("static assertion failed");
    if (!condition) return false;
    StaticAssertDecl assertion{location, active_namespace_,
                               std::move(condition), std::move(message)};
    if (active_function_ &&
        !active_function_->generic_parameters.empty())
        active_function_->deferred_static_assertions.push_back(
            std::move(assertion));
    else
        static_assertions_.push_back(std::move(assertion));
    return true;
}

ParameterDecl Parser::parse_parameter(unsigned ordinal) {
    ParameterDecl parameter;
    parameter.location = current().location;
    auto attributes = parse_attributes();
    if (consume("in")) { parameter.mode = ParameterMode::In; parameter.explicit_mode = true; }
    else if (consume("out")) { parameter.mode = ParameterMode::Out; parameter.explicit_mode = true; }
    else if (consume("inout")) { parameter.mode = ParameterMode::InOut; parameter.explicit_mode = true; }
    parameter.type = parse_type();
    std::optional<std::string> name;
    parameter.type = parse_declarator(std::move(parameter.type), name, true,
                                      nullptr, &parameter.location);
    if (parameter.mode != ParameterMode::In && parameter.type &&
        parameter.type->is_const) {
        diagnostics_.error(parameter.location,
                           "'out' and 'inout' parameter cells cannot be const");
    }
    parameter.name = name.value_or("_parameter" + std::to_string(ordinal));
    apply_callable_attributes(parameter.type, attributes);
    if (const auto* location = consume_kind(TokenKind::String)) {
        parameter.location_name = decode_string_literal(location->text);
        if (!parameter.location_name) diagnostics_.error(location->location, "invalid location string");
    }
    return parameter;
}

std::unique_ptr<FunctionDecl>
Parser::parse_function(SourceLocation location, std::string name,
                       std::string name_space, TypePtr return_type,
                       Linkage linkage, bool inline_hint,
                       std::vector<Attribute> attributes,
                       std::shared_ptr<FunctionType> signature,
                       std::vector<FunctionDecl::GenericParameter>
                           angle_parameters) {
    auto function = std::make_unique<FunctionDecl>();
    function->location = location;
    function->name = std::move(name);
    function->source_namespace = std::move(name_space);
    if (location.file)
        function->source_unit = location.file->path.generic_string();
    function->imports = active_imports_;
    function->return_type = std::move(return_type);
    function->linkage = linkage;
    function->inline_hint = inline_hint;
    function->attributes = std::move(attributes);
    function->generic_parameters = generic_parameters(function->attributes);
    if (!angle_parameters.empty()) {
        if (!function->generic_parameters.empty())
            diagnostics_.error(location,
                               "angle generic parameters cannot be combined with [[generic]]");
        else
            function->generic_parameters = std::move(angle_parameters);
    }
    if (signature) {
        function->parameters = signature->parameters;
        function->variadic = signature->variadic;
        function->result_location = signature->result_location;
        if (!signature->abi.empty() && !function->attribute("abi")) {
            function->attributes.push_back(
                {"abi", {"\"" + signature->abi + "\""}, location});
        }
        if (!function->attribute("clobber")) {
            for (const auto& resource : signature->clobbers)
                function->attributes.push_back(
                    {"clobber", {"\"" + resource + "\""}, location});
        }
        if (signature->stack_cleanup &&
            !function->attribute("stack_cleanup")) {
            function->attributes.push_back(
                {"stack_cleanup", {"\"" + *signature->stack_cleanup + "\""},
                 location});
        }
    } else {
        expect("(");
        if (!consume(")")) {
            if (current().is("void") && current(1).is(")")) {
                ++index_;
            } else {
                unsigned ordinal = 0;
                for (;;) {
                    if (consume("...")) {
                        function->variadic = true;
                        break;
                    }
                    function->parameters.push_back(parse_parameter(ordinal++));
                    if (!consume(",")) break;
                }
            }
            expect(")");
        }
    }
    if (consume("->")) {
        const auto* location_token = consume_kind(TokenKind::String);
        if (!location_token)
            error_here("expected result location string after '->'");
        else {
            function->result_location =
                decode_string_literal(location_token->text);
            if (!function->result_location) {
                diagnostics_.error(location_token->location,
                                   "invalid result location string");
            }
        }
    }
    auto trailing = parse_attributes();
    function->attributes.insert(function->attributes.end(),
                                std::make_move_iterator(trailing.begin()),
                                std::make_move_iterator(trailing.end()));
    if (consume(";")) return function;
    if (!current().is("{")) {
        error_here("expected ';' or function body");
        synchronize_external();
        return function;
    }
    auto* previous_function = active_function_;
    active_function_ = function.get();
    function->body = parse_compound();
    active_function_ = previous_function;
    return function;
}

std::unique_ptr<ObjectDecl> Parser::parse_object(
    SourceLocation location, std::string name, TypePtr type, Linkage linkage,
    std::vector<Attribute> attributes) {
    auto object = std::make_unique<ObjectDecl>();
    object->location = location;
    object->name = std::move(name);
    if (location.file) object->source_unit = location.file->path.generic_string();
    object->type = std::move(type);
    object->linkage = linkage;
    object->attributes = std::move(attributes);
    if (consume("=")) object->initializer = parse_initializer();
    if (object->type && object->type->kind == Type::Kind::Array &&
        object->type->lanes == 0 && object->initializer &&
        object->initializer->kind == Expr::Kind::String &&
        object->type->element &&
        object->type->element->kind == Type::Kind::Builtin &&
        object->type->element->builtin == BuiltinType::U8) {
        object->type->lanes = static_cast<std::uint32_t>(
            object->initializer->string_value.size() + 1);
    }
    if (object->type && object->type->kind == Type::Kind::Array &&
        object->type->lanes == 0 &&
        (!object->initializer ||
         object->initializer->kind != Expr::Kind::AggregateInitializer)) {
        diagnostics_.error(location,
                           "an omitted array bound requires a u8 string initializer");
    }
    expect(";");
    return object;
}

bool Parser::local_declaration_start() const {
    return current().is("register") || current().is("stack") ||
           current().is("static") || type_start();
}

std::unique_ptr<Statement>
Parser::parse_local_declaration(std::vector<Attribute> attributes) {
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Declaration;
    statement->location = current().location;
    statement->declaration = std::make_unique<VariableDecl>();
    auto& declaration = *statement->declaration;
    declaration.location = current().location;
    if (consume("register")) declaration.storage_register = true;
    else if (consume("stack")) declaration.storage_stack = true;
    else if (consume("static")) declaration.storage_static = true;
    declaration.type = parse_type();
    std::optional<std::string> name;
    declaration.type =
        parse_declarator(std::move(declaration.type), name, false,
                         &declaration.dynamic_array_bound, &declaration.location);
    if (!name) error_here("expected local variable name");
    else {
        declaration.name = *name;
        if (!local_scopes_.empty()) local_scopes_.back().insert(*name);
    }
    auto trailing = parse_attributes();
    attributes.insert(attributes.end(),
                      std::make_move_iterator(trailing.begin()),
                      std::make_move_iterator(trailing.end()));
    declaration.attributes = std::move(attributes);
    for (const auto& attribute : declaration.attributes) {
        if (attribute.name != "aligned") {
            diagnostics_.error(
                attribute.location,
                "attribute '" + attribute.name +
                    "' is not valid on a local object");
            continue;
        }
        // Target-dependent constants are resolved after HIR has completed
        // nominal layouts and before this local is lowered to MIR.
    }
    if (const auto* location = consume_kind(TokenKind::String)) {
        declaration.location_name = decode_string_literal(location->text);
    }
    if (consume("=")) declaration.initializer = parse_initializer();
    if (declaration.type && declaration.type->kind == Type::Kind::Array &&
        declaration.type->lanes == 0 && !declaration.dynamic_array_bound &&
        declaration.initializer &&
        declaration.initializer->kind == Expr::Kind::String &&
        declaration.type->element &&
        declaration.type->element->kind == Type::Kind::Builtin &&
        declaration.type->element->builtin == BuiltinType::U8) {
        declaration.type->lanes = static_cast<std::uint32_t>(
            declaration.initializer->string_value.size() + 1);
    }
    if (declaration.type && declaration.type->kind == Type::Kind::Array &&
        declaration.type->lanes == 0 && !declaration.dynamic_array_bound &&
        (!declaration.initializer ||
         declaration.initializer->kind !=
             Expr::Kind::AggregateInitializer)) {
        diagnostics_.error(declaration.location,
                           "an omitted array bound requires a u8 string initializer");
    }
    expect(";");
    return statement;
}

std::unique_ptr<Statement> Parser::parse_compound() {
    const auto saved_imports = active_imports_;
    const auto saved_scope_imports = current_scope_imports_;
    current_scope_imports_ = 0;
    local_scopes_.emplace_back();
    if (local_scopes_.size() == 1 && active_function_)
        for (const auto& parameter : active_function_->parameters)
            local_scopes_.back().insert(parameter.name);
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Compound;
    statement->location = current().location;
    expect("{");
    while (!current().is("}") && current().kind != TokenKind::End) {
        if (consume("using")) {
            auto declaration = std::make_unique<Statement>();
            declaration->kind = Statement::Kind::Empty;
            declaration->location = tokens_[index_ - 1].location;
            if (auto imported = parse_qualified_name()) {
                active_imports_.insert(active_imports_.begin() +
                    static_cast<std::ptrdiff_t>(current_scope_imports_),
                    std::move(*imported));
                ++current_scope_imports_;
            } else error_here("expected namespace name after 'using'");
            expect(";", "after using declaration");
            statement->statements.push_back(std::move(declaration));
        } else statement->statements.push_back(parse_statement());
    }
    expect("}");
    active_imports_ = saved_imports;
    current_scope_imports_ = saved_scope_imports;
    local_scopes_.pop_back();
    return statement;
}

std::unique_ptr<Statement> Parser::parse_statement() {
    if (current().is("{")) return parse_compound();
    if (current().is("[[")) {
        auto attributes = parse_attributes();
        if (current().is("global") && current(1).is("label") &&
            current(2).kind == TokenKind::Identifier && current(3).is(":")) {
            return parse_global_label_statement(std::move(attributes));
        }
        if (local_declaration_start()) {
            return parse_local_declaration(std::move(attributes));
        }
        if (current().is("return")) {
            auto statement = parse_statement();
            bool musttail_seen = false;
            for (auto& attribute : attributes) {
                if (attribute.name != "musttail") {
                    diagnostics_.error(
                        attribute.location,
                        "attribute '" + attribute.name +
                            "' is not valid on a return statement");
                    continue;
                }
                if (!attribute.arguments.empty()) {
                    diagnostics_.error(attribute.location,
                                       "musttail does not take arguments");
                    continue;
                }
                if (musttail_seen) {
                    diagnostics_.error(attribute.location,
                                       "a return statement has at most one musttail attribute");
                    continue;
                }
                musttail_seen = true;
                statement->attributes.push_back(std::move(attribute));
            }
            return statement;
        }
        for (const auto& attribute : attributes) {
            diagnostics_.error(
                attribute.location,
                "attribute '" + attribute.name +
                    "' is not valid on this statement");
        }
        return parse_statement();
    }
    if (current().is("$::static_assert")) {
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Empty;
        statement->location = current().location;
        (void)parse_static_assertion();
        return statement;
    }
    if (current().is("global") && current(1).is("label") &&
        current(2).kind == TokenKind::Identifier && current(3).is(":")) {
        return parse_global_label_statement();
    }
    if ((current().kind == TokenKind::Identifier && !current().is("default") && current(1).is(":")) ||
        (current().is("label") && current(1).kind == TokenKind::Identifier && current(2).is(":"))) {
        auto statement = std::make_unique<Statement>();
        statement->kind = Statement::Kind::Label;
        statement->location = current().location;
        if (consume("label")) statement->label_name = std::string(current().text);
        else statement->label_name = std::string(current().text);
        ++index_;
        expect(":");
        return statement;
    }
    if (local_declaration_start()) return parse_local_declaration();
    const auto location = current().location;
    auto statement = std::make_unique<Statement>();
    statement->location = location;
    if (consume(";")) { statement->kind = Statement::Kind::Empty; return statement; }
    if (consume("return")) {
        statement->kind = Statement::Kind::Return;
        if (!current().is(";")) statement->expression = parse_expression();
        expect(";");
        return statement;
    }
    if (consume("goto")) {
        statement->kind = Statement::Kind::Goto;
        statement->expression = parse_expression();
        expect(";");
        return statement;
    }
    if (consume("if")) {
        statement->kind = Statement::Kind::If;
        expect("("); statement->condition = parse_expression(); expect(")");
        statement->first = parse_statement();
        if (consume("else")) statement->second = parse_statement();
        return statement;
    }
    if (consume("switch")) {
        statement->kind = Statement::Kind::Switch;
        expect("("); statement->condition = parse_expression(); expect(")");
        ++switch_depth_;
        switch_default_seen_.push_back(false);
        statement->first = parse_statement();
        switch_default_seen_.pop_back();
        --switch_depth_;
        return statement;
    }
    if (consume("while")) {
        statement->kind = Statement::Kind::While;
        expect("("); statement->condition = parse_expression(); expect(")");
        statement->first = parse_statement();
        return statement;
    }
    if (consume("do")) {
        statement->kind = Statement::Kind::DoWhile;
        statement->first = parse_statement();
        expect("while", "after 'do' body");
        expect("("); statement->condition = parse_expression(); expect(")");
        expect(";");
        return statement;
    }
    if (consume("for")) {
        statement->kind = Statement::Kind::For;
        local_scopes_.emplace_back();
        expect("(");
        if (consume(";")) {
            statement->first = std::make_unique<Statement>();
            statement->first->kind = Statement::Kind::Empty;
            statement->first->location = location;
        } else if (local_declaration_start()) {
            statement->first = parse_local_declaration();
        } else if (current().is("[[")) {
            auto attributes = parse_attributes();
            if (local_declaration_start()) {
                statement->first =
                    parse_local_declaration(std::move(attributes));
            } else {
                for (const auto& attribute : attributes) {
                    diagnostics_.error(
                        attribute.location,
                        "attribute '" + attribute.name +
                            "' is not valid on a for initializer");
                }
                statement->first = std::make_unique<Statement>();
                statement->first->kind = Statement::Kind::Expression;
                statement->first->location = current().location;
                statement->first->expression = parse_expression();
                expect(";");
            }
        } else {
            statement->first = std::make_unique<Statement>();
            statement->first->kind = Statement::Kind::Expression;
            statement->first->location = current().location;
            statement->first->expression = parse_expression();
            expect(";");
        }
        if (!current().is(";")) statement->condition = parse_expression();
        expect(";");
        if (!current().is(")")) statement->increment = parse_expression();
        expect(")");
        statement->second = parse_statement();
        local_scopes_.pop_back();
        return statement;
    }
    if (consume("break")) {
        statement->kind = Statement::Kind::Break; expect(";"); return statement;
    }
    if (consume("continue")) {
        statement->kind = Statement::Kind::Continue; expect(";"); return statement;
    }
    if (consume("case")) {
        statement->kind = Statement::Kind::Case;
        if (switch_depth_ == 0) {
            error_here("case label is not inside a switch");
        }
        statement->expression = parse_expression();
        expect(":");
        if (!current().is("}")) statement->first = parse_statement();
        return statement;
    }
    if (consume("default")) {
        statement->kind = Statement::Kind::Default;
        if (switch_depth_ == 0) {
            error_here("default label is not inside a switch");
        } else if (!switch_default_seen_.empty() &&
                   switch_default_seen_.back()) {
            error_here("duplicate default label in switch");
        } else if (!switch_default_seen_.empty()) {
            switch_default_seen_.back() = true;
        }
        expect(":");
        if (!current().is("}")) statement->first = parse_statement();
        return statement;
    }
    statement->kind = Statement::Kind::Expression;
    statement->expression = parse_expression();
    expect(";");
    return statement;
}

std::unique_ptr<Statement> Parser::parse_global_label_statement(
    std::vector<Attribute> attributes) {
    auto statement = std::make_unique<Statement>();
    statement->kind = Statement::Kind::Label;
    statement->location = current().location;
    statement->global_label = true;
    statement->attributes = std::move(attributes);
    consume("global");
    consume("label");
    const auto* name = consume_kind(TokenKind::Identifier);
    if (!name) {
        error_here("expected label name after 'global label'");
    } else {
        statement->label_name = std::string(name->text);
    }
    expect(":", "after global label name");
    return statement;
}

int Parser::precedence(std::string_view operation) {
    if (operation == "||") return 1;
    if (operation == "&&") return 2;
    if (operation == "|") return 3;
    if (operation == "^") return 4;
    if (operation == "&") return 5;
    if (operation == "==" || operation == "!=") return 6;
    if (operation == "<" || operation == ">" || operation == "<=" || operation == ">=") return 7;
    if (operation == "<<" || operation == ">>") return 8;
    if (operation == "+" || operation == "-") return 9;
    if (operation == "*" || operation == "/" || operation == "%") return 10;
    return -1;
}

std::unique_ptr<Expr> Parser::parse_expression() { return parse_assignment(); }

std::unique_ptr<Expr> Parser::parse_initializer() {
    if (!current().is("{")) return parse_assignment();
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::AggregateInitializer;
    result->location = current().location;
    consume("{");
    while (!current().is("}") && current().kind != TokenKind::End) {
        Expr::InitializerEntry entry;
        entry.location = current().location;
        while (current().is(".") || current().is("[")) {
            Expr::InitializerDesignator designator;
            designator.location = current().location;
            if (consume(".")) {
                designator.kind =
                    Expr::InitializerDesignator::Kind::Member;
                const auto* member = consume_kind(TokenKind::Identifier);
                if (!member) {
                    error_here("expected member name after '.' in initializer");
                } else {
                    designator.member = std::string(member->text);
                }
            } else {
                consume("[");
                designator.kind =
                    Expr::InitializerDesignator::Kind::Index;
                designator.index = parse_assignment();
                expect("]", "after initializer designator");
            }
            entry.designators.push_back(std::move(designator));
        }
        if (!entry.designators.empty()) consume("=");
        entry.value = parse_initializer();
        result->initializer_entries.push_back(std::move(entry));
        if (!consume(",")) break;
        if (current().is("}")) break;
    }
    expect("}", "after initializer list");
    return result;
}

std::unique_ptr<Expr> Parser::parse_assignment() {
    auto left = parse_conditional();
    if (current().is("=") || current().is("+=") || current().is("-=") ||
        current().is("*=") || current().is("/=") || current().is("%=") ||
        current().is("<<=") || current().is(">>=") ||
        current().is("&=") || current().is("^=") ||
        current().is("|=")) {
        const auto operation = current(); ++index_;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Assign;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = std::move(left);
        result->right = parse_assignment();
        return result;
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_conditional() {
    auto condition = parse_binary(1);
    if (!consume("?")) return condition;
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Conditional;
    result->location = condition->location;
    result->left = std::move(condition);
    result->right = parse_expression();
    expect(":");
    result->third = parse_conditional();
    return result;
}

std::unique_ptr<Expr> Parser::parse_binary(int minimum_precedence) {
    auto left = parse_cast();
    for (;;) {
        if (parsing_generic_argument_ &&
            (current().is(">") || current().is(">>"))) break;
        const int current_precedence = precedence(current().text);
        if (current_precedence < minimum_precedence) break;
        const auto operation = current(); ++index_;
        auto right = parse_binary(current_precedence + 1);
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Binary;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = std::move(left);
        result->right = std::move(right);
        left = std::move(result);
    }
    return left;
}

std::unique_ptr<Expr> Parser::parse_cast() {
    if (current().is("(")) {
        const auto saved = index_;
        ++index_;
        const bool begins_type = type_start();
        index_ = saved;
        if (begins_type) {
            const auto location = current().location;
            consume("(");
            auto type = parse_type();
            std::optional<std::string> name;
            type = parse_declarator(std::move(type), name);
            if (name) {
                diagnostics_.error(location,
                                   "a cast type name cannot declare an object");
            }
            expect(")", "after cast type");
            auto result = std::make_unique<Expr>();
            result->kind = Expr::Kind::Cast;
            result->location = location;
            result->type = std::move(type);
            result->left = parse_cast();
            return result;
        }
    }
    return parse_unary();
}

std::unique_ptr<Expr> Parser::parse_unary() {
    if (current().is("sizeof")) {
        const auto location = current().location;
        consume("sizeof");
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Sizeof;
        result->location = location;
        if (current().is("(")) {
            const auto saved = index_;
            ++index_;
            const bool begins_type = type_start();
            index_ = saved;
            if (begins_type) {
                consume("(");
                result->type = parse_type();
                std::optional<std::string> name;
                result->type = parse_declarator(std::move(result->type), name);
                if (name) {
                    diagnostics_.error(
                        location,
                        "a sizeof type name cannot declare an object");
                }
                expect(")", "after sizeof type");
                return result;
            }
        }
        result->left = parse_unary();
        return result;
    }
    if (current().is("+") || current().is("-") || current().is("!") ||
        current().is("~") || current().is("&") || current().is("*") ||
        current().is("++") || current().is("--")) {
        const auto operation = current(); ++index_;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Unary;
        result->location = operation.location;
        result->text = std::string(operation.text);
        result->left = parse_unary();
        return result;
    }
    return parse_postfix();
}

std::unique_ptr<Expr> Parser::parse_postfix() {
    auto expression = parse_primary();
    for (;;) {
        const bool explicit_generic = current().is("::") &&
                                      current(1).is("<");
        const bool inferred_generic = current().is("<") && expression &&
                                      expression->kind == Expr::Kind::Name &&
                                      known_generic_name(expression->text);
        if (explicit_generic || inferred_generic) {
            index_ += explicit_generic ? 2 : 1;
            if (consume(">")) {
                error_here("a generic argument list cannot be empty");
            } else {
                for (;;) {
                    Expr::GenericArgument argument;
                    if (type_start()) {
                        argument.type = parse_type();
                    } else {
                        const bool previous = parsing_generic_argument_;
                        parsing_generic_argument_ = true;
                        argument.value = parse_assignment();
                        parsing_generic_argument_ = previous;
                    }
                    expression->generic_arguments.push_back(std::move(argument));
                    if (!consume(",")) break;
                }
                if (!consume_generic_close())
                    error_here("expected '>' to close generic argument list");
            }
            continue;
        }
        if (consume("(")) {
            const bool previous = parsing_generic_argument_;
            parsing_generic_argument_ = false;
            auto call = std::make_unique<Expr>();
            call->kind = Expr::Kind::Call;
            call->location = expression->location;
            call->generic_visible_at_call =
                expression->kind == Expr::Kind::Name &&
                known_generic_name(expression->text);
            call->generic_arguments = std::move(expression->generic_arguments);
            call->left = std::move(expression);
            if (!consume(")")) {
                do { call->arguments.push_back(parse_assignment()); } while (consume(","));
                expect(")");
            }
            parsing_generic_argument_ = previous;
            expression = std::move(call);
            continue;
        }
        if (consume("[")) {
            const bool previous = parsing_generic_argument_;
            parsing_generic_argument_ = false;
            auto index = std::make_unique<Expr>();
            index->kind = Expr::Kind::Binary;
            index->location = expression->location;
            index->text = "index";
            index->left = std::move(expression);
            index->right = parse_expression();
            expect("]");
            parsing_generic_argument_ = previous;
            expression = std::move(index);
            continue;
        }
        if (current().is(".") || current().is("->")) {
            const bool through_pointer = consume("->");
            if (!through_pointer) consume(".");
            const auto* member_name = consume_kind(TokenKind::Identifier);
            if (!member_name) {
                error_here("expected member name after '" +
                           std::string(through_pointer ? "->" : ".") + "'");
                break;
            }
            auto member = std::make_unique<Expr>();
            member->kind = Expr::Kind::Binary;
            member->location = member_name->location;
            member->text = through_pointer ? "pointer_member" : "member";
            member->left = std::move(expression);
            member->right = std::make_unique<Expr>();
            member->right->kind = Expr::Kind::Name;
            member->right->location = member_name->location;
            member->right->text = std::string(member_name->text);
            expression = std::move(member);
            continue;
        }
        if (current().is("++") || current().is("--")) {
            const auto operation = current();
            ++index_;
            auto update = std::make_unique<Expr>();
            update->kind = Expr::Kind::Unary;
            update->location = operation.location;
            update->text = operation.is("++") ? "post++" : "post--";
            update->left = std::move(expression);
            expression = std::move(update);
            continue;
        }
        break;
    }
    return expression;
}

std::unique_ptr<Statement> Parser::parse_procedural_body() {
    parsing_procedural_body_ = true;
    auto body = parse_compound();
    if (current().kind != TokenKind::End)
        error_here("unexpected tokens after procedural macro body");
    parsing_procedural_body_ = false;
    return body;
}

std::unique_ptr<Expr> Parser::parse_quote() {
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Quote;
    result->location = current().location;
    if (!parsing_procedural_body_)
        error_here("$::quote is only available in translation-time macro bodies");
    ++index_;
    if (!expect("{", "after $::quote")) return result;
    TokenSequence literal;
    std::vector<std::string_view> closers{"}"};
    while (current().kind != TokenKind::End) {
        if (current().is("$::unquote")) {
            result->quote_fragments.push_back(std::move(literal));
            literal.clear();
            ++index_;
            if (!expect("(", "after $::unquote")) return result;
            result->arguments.push_back(parse_expression());
            if (!expect(")", "after $::unquote expression")) return result;
            continue;
        }
        if (current().is("(")) closers.push_back(")");
        else if (current().is("[")) closers.push_back("]");
        else if (current().is("[[")) closers.push_back("]]");
        else if (current().is("{")) closers.push_back("}");
        else if (current().is(")") || current().is("]") ||
                 current().is("]]") || current().is("}")) {
            if (closers.empty() || closers.back() != current().text) {
                error_here("$::quote requires balanced token groups");
                return result;
            }
            closers.pop_back();
            if (closers.empty()) {
                result->quote_fragments.push_back(std::move(literal));
                ++index_;
                return result;
            }
        }
        literal.emplace_back(current());
        ++index_;
    }
    error_here("unterminated $::quote token tree");
    return result;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    const auto item = current();
    if (current().is("$::quote")) return parse_quote();
    if (current().is("$::unquote"))
        error_here("$::unquote is only valid inside $::quote");
    if (current().is("$::alignof") && current(1).is("(")) {
        index_ += 2;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Alignof;
        result->location = item.location;
        if (type_start()) {
            result->type = parse_type();
            std::optional<std::string> name;
            result->type = parse_declarator(std::move(result->type), name);
            if (name) {
                diagnostics_.error(
                    item.location,
                    "an alignof type name cannot declare an object");
            }
        } else {
            result->left = parse_expression();
        }
        expect(")", "after alignof operand");
        return result;
    }
    if (consume("(")) {
        const bool previous = parsing_generic_argument_;
        parsing_generic_argument_ = false;
        auto inner = parse_expression();
        expect(")");
        parsing_generic_argument_ = previous;
        auto result = std::make_unique<Expr>();
        result->kind = Expr::Kind::Parenthesized;
        result->location = item.location;
        result->left = std::move(inner);
        return result;
    }
    auto result = std::make_unique<Expr>();
    result->location = item.location;
    result->text = std::string(item.text);
    if (item.kind == TokenKind::Integer) result->kind = Expr::Kind::Integer;
    else if (item.kind == TokenKind::Floating) result->kind = Expr::Kind::Floating;
    else if (item.kind == TokenKind::String) {
        result->kind = Expr::Kind::String;
        while (current().kind == TokenKind::String) {
            const auto decoded = decode_string_literal(current().text);
            if (!decoded) {
                diagnostics_.error(current().location,
                                   "invalid UTF-8 string literal");
            } else {
                result->string_value += *decoded;
            }
            ++index_;
        }
        return result;
    }
    else if (item.kind == TokenKind::Character) result->kind = Expr::Kind::Character;
    else if (item.kind == TokenKind::Identifier || item.kind == TokenKind::BuiltinName) {
        result->kind = Expr::Kind::Name;
        if (item.kind == TokenKind::Identifier) {
            if (auto name = parse_qualified_name()) result->text = *name;
            const auto origin = token_origin(item.location);
            auto context = std::make_shared<NameLookupContext>();
            context->name_space = origin.context ? origin.context->name_space : active_namespace_;
            context->imports = origin.context ? origin.context->imports : active_imports_;
            result->name_context = std::move(context);
            return result;
        }
    } else {
        error_here("expected expression");
        result->text = "0";
        result->kind = Expr::Kind::Integer;
    }
    ++index_;
    return result;
}

} // namespace cross
