// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/model.hpp"

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "target/target.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace cross {
namespace {

enum class ModelTokenKind {
    End,
    Identifier,
    String,
    Integer,
    LeftBrace,
    RightBrace,
    LeftBracket,
    RightBracket,
    LeftParen,
    RightParen,
    Equal,
    Comma,
    Semicolon,
    Invalid,
};

struct ModelToken {
    ModelTokenKind kind{ModelTokenKind::Invalid};
    std::string text;
    unsigned line{1};
};

class ModelLexer {
public:
    explicit ModelLexer(std::string_view input) : input_(input) {}

    ModelToken next() {
        skip_trivia();
        const unsigned line = line_;
        if (offset_ == input_.size()) return {ModelTokenKind::End, {}, line};
        const char ch = input_[offset_];
        switch (ch) {
        case '{': ++offset_; return {ModelTokenKind::LeftBrace, "{", line};
        case '}': ++offset_; return {ModelTokenKind::RightBrace, "}", line};
        case '[': ++offset_; return {ModelTokenKind::LeftBracket, "[", line};
        case ']': ++offset_; return {ModelTokenKind::RightBracket, "]", line};
        case '(': ++offset_; return {ModelTokenKind::LeftParen, "(", line};
        case ')': ++offset_; return {ModelTokenKind::RightParen, ")", line};
        case '=': ++offset_; return {ModelTokenKind::Equal, "=", line};
        case ',': ++offset_; return {ModelTokenKind::Comma, ",", line};
        case ';': ++offset_; return {ModelTokenKind::Semicolon, ";", line};
        case '"': return string();
        default: break;
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            const auto begin = offset_++;
            while (offset_ < input_.size() &&
                   std::isdigit(static_cast<unsigned char>(input_[offset_])) != 0) {
                ++offset_;
            }
            return {ModelTokenKind::Integer,
                    std::string(input_.substr(begin, offset_ - begin)), line};
        }
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_') {
            const auto begin = offset_++;
            while (offset_ < input_.size()) {
                const char item = input_[offset_];
                if (std::isalnum(static_cast<unsigned char>(item)) == 0 &&
                    item != '_' && item != '-' && item != '.') {
                    break;
                }
                ++offset_;
            }
            return {ModelTokenKind::Identifier,
                    std::string(input_.substr(begin, offset_ - begin)), line};
        }
        ++offset_;
        return {ModelTokenKind::Invalid, std::string(1, ch), line};
    }

private:
    void skip_trivia() {
        for (;;) {
            while (offset_ < input_.size() &&
                   std::isspace(static_cast<unsigned char>(input_[offset_])) != 0) {
                if (input_[offset_++] == '\n') ++line_;
            }
            if (offset_ < input_.size() && input_[offset_] == '#') {
                while (offset_ < input_.size() && input_[offset_] != '\n') {
                    ++offset_;
                }
                continue;
            }
            if (offset_ + 1 < input_.size() && input_[offset_] == '/' &&
                input_[offset_ + 1] == '/') {
                offset_ += 2;
                while (offset_ < input_.size() && input_[offset_] != '\n') {
                    ++offset_;
                }
                continue;
            }
            return;
        }
    }

    ModelToken string() {
        const unsigned line = line_;
        ++offset_;
        std::string value;
        while (offset_ < input_.size() && input_[offset_] != '"') {
            char ch = input_[offset_++];
            if (ch == '\n') {
                ++line_;
                return {ModelTokenKind::Invalid,
                        "newline in model string", line};
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }
            if (offset_ == input_.size()) {
                return {ModelTokenKind::Invalid,
                        "unterminated model string", line};
            }
            ch = input_[offset_++];
            switch (ch) {
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case '\\': value.push_back('\\'); break;
            case '"': value.push_back('"'); break;
            default:
                return {ModelTokenKind::Invalid,
                        "unsupported model string escape", line};
            }
        }
        if (offset_ == input_.size()) {
            return {ModelTokenKind::Invalid, "unterminated model string", line};
        }
        ++offset_;
        return {ModelTokenKind::String, std::move(value), line};
    }

    std::string_view input_;
    std::size_t offset_{};
    unsigned line_{1};
};

struct ModelValue {
    enum class Kind { Text, Integer, Boolean, List } kind{Kind::Text};
    std::string text;
    std::uint64_t integer{};
    bool boolean{};
    std::vector<std::string> list;
};

struct ModelProperty {
    ModelValue value;
    unsigned line{};
};

using ModelProperties = std::unordered_map<std::string, ModelProperty>;

struct ModelDocument {
    std::vector<AbiEntry> abis;
    std::vector<ManglingEntry> manglings;
    std::vector<OptimizationEntry> optimizations;
    std::vector<ProfileEntry> profiles;
};

class ModelParser {
public:
    ModelParser(std::string_view text, std::string origin)
        : lexer_(text), origin_(std::move(origin)), current_(lexer_.next()) {}

    std::optional<ModelDocument> parse() {
        ModelDocument result;
        while (current_.kind != ModelTokenKind::End && error_.empty()) {
            if (current_.kind != ModelTokenKind::Identifier) {
                fail(current_.line,
                     "expected model domain: abi, mangling, optimization, "
                     "or profile");
                break;
            }
            const auto domain = current_.text;
            const auto line = current_.line;
            advance();
            if (domain != "abi" && domain != "mangling" &&
                domain != "optimization" && domain != "profile") {
                fail(line, "unknown model domain '" + domain + "'");
                break;
            }
            if (current_.kind != ModelTokenKind::String) {
                fail(current_.line, "expected quoted model entry name");
                break;
            }
            auto name = current_.text;
            advance();
            if (!expect(ModelTokenKind::LeftBrace, "'{' after model entry name")) {
                break;
            }
            if (domain == "mangling") {
                if (auto entry = parse_mangling(std::move(name), line)) {
                    result.manglings.push_back(std::move(*entry));
                }
                continue;
            }
            if (domain == "abi") {
                if (auto entry = parse_abi(std::move(name), line)) {
                    result.abis.push_back(std::move(*entry));
                }
                continue;
            }
            auto properties = parse_properties();
            if (!properties || !error_.empty()) break;
            if (domain == "optimization") {
                if (auto entry =
                        make_optimization(name, *properties, line)) {
                    result.optimizations.push_back(std::move(*entry));
                }
            } else if (auto entry = make_profile(name, *properties, line)) {
                result.profiles.push_back(std::move(*entry));
            }
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    [[nodiscard]] const std::string& error() const { return error_; }

private:
    void advance() { current_ = lexer_.next(); }

    bool expect(ModelTokenKind kind, std::string_view context) {
        if (current_.kind == kind) {
            advance();
            return true;
        }
        fail(current_.line, "expected " + std::string(context));
        return false;
    }

    void fail(unsigned line, std::string message) {
        if (error_.empty()) {
            error_ = origin_ + ':' + std::to_string(line) + ": " +
                     std::move(message);
        }
    }

    struct ManglingScope {
        bool entity{};
        bool item{};
        bool arguments{};
        bool parameter{};
        bool substitution{};
        const std::unordered_map<std::string, ManglingValueType>* variables{};
        const std::unordered_map<std::string, ManglingHelper>* helpers{};
    };

    struct AbiNestedBlock {
        std::string name;
        ModelProperties properties;
        unsigned line{};
    };

    std::optional<ManglingExpression> parse_mangling_expression(
        unsigned depth = 0) {
        if (depth >= 64) {
            fail(current_.line,
                 "mangler DSL expression nesting exceeds 64 levels");
            return std::nullopt;
        }
        ManglingExpression result;
        result.line = current_.line;
        if (current_.kind == ModelTokenKind::String) {
            result.kind = ManglingExpressionKind::Literal;
            result.literal = current_.text;
            advance();
            return result;
        }
        if (current_.kind == ModelTokenKind::Integer) {
            result.kind = ManglingExpressionKind::IntegerLiteral;
            const auto text = current_.text;
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), result.integer);
            if (parsed.ec != std::errc{} ||
                parsed.ptr != text.data() + text.size()) {
                fail(result.line, "invalid mangler DSL integer literal");
                return std::nullopt;
            }
            advance();
            return result;
        }
        if (current_.kind != ModelTokenKind::Identifier) {
            fail(current_.line, "expected mangler DSL expression");
            return std::nullopt;
        }

        const auto spelling = current_.text;
        advance();
        if (current_.kind != ModelTokenKind::LeftParen) {
            if (spelling == "true" || spelling == "false") {
                result.kind = ManglingExpressionKind::BooleanLiteral;
                result.boolean = spelling == "true";
                return result;
            }
            static constexpr std::pair<std::string_view,
                                       ManglingExpressionKind>
                values[] = {
                    {"name", ManglingExpressionKind::Name},
                    {"entity", ManglingExpressionKind::Entity},
                    {"kind", ManglingExpressionKind::Kind},
                    {"result", ManglingExpressionKind::Result},
                    {"variadic", ManglingExpressionKind::Variadic},
                    {"parameter_count",
                     ManglingExpressionKind::ParameterCount},
                    {"text", ManglingExpressionKind::Text},
                    {"mode", ManglingExpressionKind::Mode},
                    {"index", ManglingExpressionKind::Index},
                    {"count", ManglingExpressionKind::Count},
                    {"substitution_index",
                     ManglingExpressionKind::SubstitutionIndex},
                };
            const auto found = std::find_if(
                std::begin(values), std::end(values),
                [&](const auto& entry) { return entry.first == spelling; });
            if (found == std::end(values)) {
                result.kind = ManglingExpressionKind::Variable;
                result.literal = spelling;
                return result;
            }
            result.kind = found->second;
            return result;
        }

        static constexpr std::pair<std::string_view,
                                   ManglingExpressionKind>
            operations[] = {
                {"concat", ManglingExpressionKind::Concat},
                {"decimal", ManglingExpressionKind::Decimal},
                {"hex", ManglingExpressionKind::Hex},
                {"radix", ManglingExpressionKind::Radix},
                {"bytes", ManglingExpressionKind::Bytes},
                {"length", ManglingExpressionKind::Length},
                {"path", ManglingExpressionKind::Path},
                {"arguments", ManglingExpressionKind::Arguments},
                {"parameters", ManglingExpressionKind::Parameters},
                {"select", ManglingExpressionKind::Select},
                {"equal", ManglingExpressionKind::Equal},
                {"not", ManglingExpressionKind::Not},
                {"and", ManglingExpressionKind::And},
                {"or", ManglingExpressionKind::Or},
                {"add", ManglingExpressionKind::Add},
                {"subtract", ManglingExpressionKind::Subtract},
                {"slice", ManglingExpressionKind::Slice},
                {"replace", ManglingExpressionKind::Replace},
                {"lookup", ManglingExpressionKind::Lookup},
                {"lower", ManglingExpressionKind::Lower},
                {"upper", ManglingExpressionKind::Upper},
                {"starts_with", ManglingExpressionKind::StartsWith},
                {"ends_with", ManglingExpressionKind::EndsWith},
                {"contains", ManglingExpressionKind::Contains},
                {"substitute", ManglingExpressionKind::Substitute},
            };
        const auto operation = std::find_if(
            std::begin(operations), std::end(operations),
            [&](const auto& entry) { return entry.first == spelling; });
        if (operation == std::end(operations)) {
            result.kind = ManglingExpressionKind::HelperCall;
            result.literal = spelling;
        } else {
            result.kind = operation->second;
        }
        advance();
        while (current_.kind != ModelTokenKind::RightParen &&
               current_.kind != ModelTokenKind::End) {
            auto operand = parse_mangling_expression(depth + 1);
            if (!operand) return std::nullopt;
            result.operands.push_back(std::move(*operand));
            if (current_.kind == ModelTokenKind::RightParen) break;
            if (!expect(ModelTokenKind::Comma,
                        "',' between mangler DSL operands")) {
                return std::nullopt;
            }
        }
        if (!expect(ModelTokenKind::RightParen,
                    "')' after mangler DSL operation")) {
            return std::nullopt;
        }
        return result;
    }

    std::optional<ManglingValueType> validate_mangling_expression(
        const ManglingExpression& expression, ManglingScope scope) {
        const auto exact_operands = [&](std::size_t count,
                                        std::string_view name) {
            if (expression.operands.size() == count) return true;
            fail(expression.line,
                 "mangler DSL operation '" + std::string(name) +
                     "' requires " + std::to_string(count) + " operand(s)");
            return false;
        };
        const auto require = [&](const ManglingExpression& operand,
                                 ManglingScope operand_scope,
                                 ManglingValueType expected,
                                 std::string_view operation)
            -> bool {
            const auto actual =
                validate_mangling_expression(operand, operand_scope);
            if (!actual) return false;
            if (*actual == expected) return true;
            fail(operand.line,
                 "mangler DSL operation '" + std::string(operation) +
                     "' received an operand of the wrong type");
            return false;
        };

        switch (expression.kind) {
        case ManglingExpressionKind::Literal:
        case ManglingExpressionKind::Name:
            return ManglingValueType::Text;
        case ManglingExpressionKind::IntegerLiteral:
            return ManglingValueType::Integer;
        case ManglingExpressionKind::BooleanLiteral:
            return ManglingValueType::Boolean;
        case ManglingExpressionKind::Entity:
            if (!scope.entity) {
                fail(expression.line,
                     "mangler DSL value 'entity' is available only in the "
                     "generic rule");
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Kind:
        case ManglingExpressionKind::Result:
            return ManglingValueType::Text;
        case ManglingExpressionKind::Variadic:
            return ManglingValueType::Boolean;
        case ManglingExpressionKind::ParameterCount:
            return ManglingValueType::Integer;
        case ManglingExpressionKind::Text:
            if (!scope.item) {
                fail(expression.line,
                     "mangler DSL value 'text' is available only inside "
                     "path or argument mapping");
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Mode:
            if (!scope.parameter) {
                fail(expression.line,
                     "mangler DSL value 'mode' is available only inside "
                     "parameter mapping");
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Index:
            if (!scope.item) {
                fail(expression.line,
                     "mangler DSL value 'index' is available only inside "
                     "path or argument mapping");
                return std::nullopt;
            }
            return ManglingValueType::Integer;
        case ManglingExpressionKind::Count:
            if (!scope.item && !scope.arguments) {
                fail(expression.line,
                     "mangler DSL value 'count' requires a generic rule or "
                     "a mapped item");
                return std::nullopt;
            }
            return ManglingValueType::Integer;
        case ManglingExpressionKind::SubstitutionIndex:
            if (!scope.substitution) {
                fail(expression.line,
                     "mangler DSL value 'substitution_index' is available "
                     "only in a substitute reference expression");
                return std::nullopt;
            }
            return ManglingValueType::Integer;
        case ManglingExpressionKind::Variable: {
            if (!scope.variables) {
                fail(expression.line,
                     "unknown mangler DSL value '" +
                         expression.literal + "'");
                return std::nullopt;
            }
            const auto found =
                scope.variables->find(expression.literal);
            if (found == scope.variables->end()) {
                fail(expression.line,
                     "unknown mangler DSL value '" +
                         expression.literal + "'");
                return std::nullopt;
            }
            return found->second;
        }
        case ManglingExpressionKind::HelperCall: {
            if (!scope.helpers) {
                fail(expression.line,
                     "unknown mangler helper '" +
                         expression.literal + "'");
                return std::nullopt;
            }
            const auto found =
                scope.helpers->find(expression.literal);
            if (found == scope.helpers->end()) {
                fail(expression.line,
                     "unknown mangler helper '" +
                         expression.literal + "'");
                return std::nullopt;
            }
            const auto& helper = found->second;
            if (expression.operands.size() !=
                helper.parameters.size()) {
                fail(expression.line,
                     "mangler helper '" + expression.literal +
                         "' received the wrong number of arguments");
                return std::nullopt;
            }
            for (std::size_t index = 0;
                 index < expression.operands.size(); ++index) {
                if (!require(
                        expression.operands[index], scope,
                        helper.parameters[index].type,
                        expression.literal)) {
                    return std::nullopt;
                }
            }
            return helper.result_type;
        }
        case ManglingExpressionKind::Concat:
            if (expression.operands.empty()) {
                fail(expression.line,
                     "mangler DSL operation 'concat' requires at least one "
                     "operand");
                return std::nullopt;
            }
            for (const auto& operand : expression.operands) {
                if (!require(operand, scope, ManglingValueType::Text,
                             "concat")) {
                    return std::nullopt;
                }
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Decimal:
            if (!exact_operands(1, "decimal") ||
                !require(expression.operands.front(), scope,
                         ManglingValueType::Integer, "decimal")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Hex:
            if (!exact_operands(1, "hex") ||
                !require(expression.operands.front(), scope,
                         ManglingValueType::Integer, "hex")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Radix:
            if (!exact_operands(2, "radix") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Integer, "radix") ||
                !require(expression.operands[1], scope,
                         ManglingValueType::Integer, "radix")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Bytes:
        case ManglingExpressionKind::Length: {
            const auto name =
                expression.kind == ManglingExpressionKind::Bytes
                    ? std::string_view{"bytes"}
                    : std::string_view{"length"};
            if (!exact_operands(1, name) ||
                !require(expression.operands.front(), scope,
                         ManglingValueType::Text, name)) {
                return std::nullopt;
            }
            return ManglingValueType::Integer;
        }
        case ManglingExpressionKind::Path: {
            if ((expression.operands.size() != 2 &&
                 expression.operands.size() != 3) ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, "path")) {
                if (expression.operands.size() != 2 &&
                    expression.operands.size() != 3) {
                    fail(expression.line,
                         "mangler DSL operation 'path' requires two or "
                         "three operands");
                }
                return std::nullopt;
            }
            auto item_scope = scope;
            item_scope.item = true;
            if (!require(expression.operands[1], item_scope,
                         ManglingValueType::Text, "path")) {
                return std::nullopt;
            }
            if (expression.operands.size() == 3 &&
                !require(expression.operands[2], scope,
                         ManglingValueType::Boolean, "path")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        }
        case ManglingExpressionKind::Arguments: {
            if (!scope.arguments) {
                fail(expression.line,
                     "mangler DSL operation 'arguments' is available only "
                     "in the generic rule");
                return std::nullopt;
            }
            if (!exact_operands(3, "arguments") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, "arguments")) {
                return std::nullopt;
            }
            auto item_scope = scope;
            item_scope.item = true;
            if (!require(expression.operands[1], item_scope,
                         ManglingValueType::Text, "arguments") ||
                !require(expression.operands[2], item_scope,
                         ManglingValueType::Text, "arguments")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        }
        case ManglingExpressionKind::Parameters: {
            if (!exact_operands(2, "parameters") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, "parameters")) {
                return std::nullopt;
            }
            auto item_scope = scope;
            item_scope.item = true;
            item_scope.parameter = true;
            if (!require(expression.operands[1], item_scope,
                         ManglingValueType::Text, "parameters")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        }
        case ManglingExpressionKind::Select: {
            if (!exact_operands(3, "select") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Boolean, "select")) {
                return std::nullopt;
            }
            const auto left =
                validate_mangling_expression(expression.operands[1], scope);
            const auto right =
                validate_mangling_expression(expression.operands[2], scope);
            if (!left || !right) return std::nullopt;
            if (*left != *right) {
                fail(expression.line,
                     "mangler DSL operation 'select' requires branches of "
                     "the same type");
                return std::nullopt;
            }
            return left;
        }
        case ManglingExpressionKind::Equal: {
            if (!exact_operands(2, "equal")) return std::nullopt;
            const auto left =
                validate_mangling_expression(expression.operands[0], scope);
            const auto right =
                validate_mangling_expression(expression.operands[1], scope);
            if (!left || !right) return std::nullopt;
            if (*left != *right) {
                fail(expression.line,
                     "mangler DSL operation 'equal' requires operands of "
                     "the same type");
                return std::nullopt;
            }
            return ManglingValueType::Boolean;
        }
        case ManglingExpressionKind::Not:
            if (!exact_operands(1, "not") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Boolean, "not")) {
                return std::nullopt;
            }
            return ManglingValueType::Boolean;
        case ManglingExpressionKind::And:
        case ManglingExpressionKind::Or: {
            const auto name =
                expression.kind == ManglingExpressionKind::And
                    ? std::string_view{"and"}
                    : std::string_view{"or"};
            if (expression.operands.size() < 2) {
                fail(expression.line,
                     "mangler DSL operation '" + std::string(name) +
                         "' requires at least two operands");
                return std::nullopt;
            }
            for (const auto& operand : expression.operands) {
                if (!require(operand, scope, ManglingValueType::Boolean,
                             name)) {
                    return std::nullopt;
                }
            }
            return ManglingValueType::Boolean;
        }
        case ManglingExpressionKind::Add:
        case ManglingExpressionKind::Subtract: {
            const auto name =
                expression.kind == ManglingExpressionKind::Add
                    ? std::string_view{"add"}
                    : std::string_view{"subtract"};
            if (!exact_operands(2, name) ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Integer, name) ||
                !require(expression.operands[1], scope,
                         ManglingValueType::Integer, name)) {
                return std::nullopt;
            }
            return ManglingValueType::Integer;
        }
        case ManglingExpressionKind::Slice:
            if (!exact_operands(3, "slice") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, "slice") ||
                !require(expression.operands[1], scope,
                         ManglingValueType::Integer, "slice") ||
                !require(expression.operands[2], scope,
                         ManglingValueType::Integer, "slice")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Replace:
            if (!exact_operands(3, "replace")) return std::nullopt;
            for (const auto& operand : expression.operands) {
                if (!require(operand, scope, ManglingValueType::Text,
                             "replace")) {
                    return std::nullopt;
                }
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Lookup:
            if (expression.operands.size() < 4 ||
                expression.operands.size() % 2 != 0) {
                fail(expression.line,
                     "mangler DSL operation 'lookup' requires a key, a "
                     "default, and one or more match/result pairs");
                return std::nullopt;
            }
            for (const auto& operand : expression.operands) {
                if (!require(operand, scope, ManglingValueType::Text,
                             "lookup")) {
                    return std::nullopt;
                }
            }
            return ManglingValueType::Text;
        case ManglingExpressionKind::Lower:
        case ManglingExpressionKind::Upper: {
            const auto name =
                expression.kind == ManglingExpressionKind::Lower
                    ? std::string_view{"lower"}
                    : std::string_view{"upper"};
            if (!exact_operands(1, name) ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, name)) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        }
        case ManglingExpressionKind::StartsWith:
        case ManglingExpressionKind::EndsWith:
        case ManglingExpressionKind::Contains: {
            const auto name =
                expression.kind == ManglingExpressionKind::StartsWith
                    ? std::string_view{"starts_with"}
                    : expression.kind == ManglingExpressionKind::EndsWith
                          ? std::string_view{"ends_with"}
                          : std::string_view{"contains"};
            if (!exact_operands(2, name) ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, name) ||
                !require(expression.operands[1], scope,
                         ManglingValueType::Text, name)) {
                return std::nullopt;
            }
            return ManglingValueType::Boolean;
        }
        case ManglingExpressionKind::Substitute: {
            if (!exact_operands(3, "substitute") ||
                !require(expression.operands[0], scope,
                         ManglingValueType::Text, "substitute") ||
                !require(expression.operands[1], scope,
                         ManglingValueType::Text, "substitute")) {
                return std::nullopt;
            }
            auto reference_scope = scope;
            reference_scope.substitution = true;
            if (!require(expression.operands[2], reference_scope,
                         ManglingValueType::Text, "substitute")) {
                return std::nullopt;
            }
            return ManglingValueType::Text;
        }
        }
        return std::nullopt;
    }

    std::optional<ManglingEntry> parse_mangling(
        std::string name, unsigned entry_line) {
        struct Rule {
            ManglingExpression expression;
            unsigned line{};
        };
        std::unordered_map<std::string, Rule> rules;
        std::unordered_map<std::string, ManglingHelper> helpers;
        const auto parse_type =
            [&](std::string_view spelling)
                -> std::optional<ManglingValueType> {
            if (spelling == "text") return ManglingValueType::Text;
            if (spelling == "integer") {
                return ManglingValueType::Integer;
            }
            if (spelling == "boolean") {
                return ManglingValueType::Boolean;
            }
            return std::nullopt;
        };
        while (current_.kind != ModelTokenKind::RightBrace &&
               current_.kind != ModelTokenKind::End) {
            if (current_.kind != ModelTokenKind::Identifier) {
                fail(current_.line, "expected mangler rule name");
                return std::nullopt;
            }
            if (current_.text == "helper") {
                const auto line = current_.line;
                advance();
                if (current_.kind != ModelTokenKind::Identifier) {
                    fail(current_.line,
                         "expected mangler helper result type");
                    return std::nullopt;
                }
                const auto result_type = parse_type(current_.text);
                if (!result_type) {
                    fail(current_.line,
                         "mangler helper type must be text, integer, or "
                         "boolean");
                    return std::nullopt;
                }
                advance();
                if (current_.kind != ModelTokenKind::Identifier) {
                    fail(current_.line, "expected mangler helper name");
                    return std::nullopt;
                }
                ManglingHelper helper;
                helper.name = current_.text;
                helper.result_type = *result_type;
                helper.line = line;
                advance();
                if (!expect(ModelTokenKind::LeftParen,
                            "'(' after mangler helper name")) {
                    return std::nullopt;
                }
                std::unordered_set<std::string> parameter_names;
                while (current_.kind != ModelTokenKind::RightParen &&
                       current_.kind != ModelTokenKind::End) {
                    if (current_.kind != ModelTokenKind::Identifier) {
                        fail(current_.line,
                             "expected mangler helper parameter type");
                        return std::nullopt;
                    }
                    const auto parameter_type =
                        parse_type(current_.text);
                    if (!parameter_type) {
                        fail(current_.line,
                             "mangler helper parameter type must be text, "
                             "integer, or boolean");
                        return std::nullopt;
                    }
                    advance();
                    if (current_.kind != ModelTokenKind::Identifier) {
                        fail(current_.line,
                             "expected mangler helper parameter name");
                        return std::nullopt;
                    }
                    const auto parameter_name = current_.text;
                    if (!parameter_names.insert(parameter_name).second) {
                        fail(current_.line,
                             "duplicate mangler helper parameter '" +
                                 parameter_name + "'");
                        return std::nullopt;
                    }
                    helper.parameters.push_back(
                        {parameter_name, *parameter_type});
                    advance();
                    if (current_.kind == ModelTokenKind::RightParen) {
                        break;
                    }
                    if (!expect(
                            ModelTokenKind::Comma,
                            "',' between mangler helper parameters")) {
                        return std::nullopt;
                    }
                }
                if (!expect(ModelTokenKind::RightParen,
                            "')' after mangler helper parameters") ||
                    !expect(ModelTokenKind::Equal,
                            "'=' after mangler helper signature")) {
                    return std::nullopt;
                }
                auto expression = parse_mangling_expression();
                if (!expression) return std::nullopt;
                helper.expression = std::move(*expression);
                if (!expect(ModelTokenKind::Semicolon,
                            "';' after mangler helper")) {
                    return std::nullopt;
                }
                const auto helper_key = helper.name;
                if (helper_key == "entity" ||
                    helper_key == "label" ||
                    helper_key == "generic" ||
                    !helpers.emplace(helper_key, std::move(helper)).second) {
                    fail(line, "duplicate or reserved mangler helper name");
                    return std::nullopt;
                }
                continue;
            }
            const auto rule_name = current_.text;
            const auto line = current_.line;
            advance();
            if (!expect(ModelTokenKind::Equal,
                        "'=' after mangler rule name")) {
                return std::nullopt;
            }
            auto expression = parse_mangling_expression();
            if (!expression) return std::nullopt;
            if (!expect(ModelTokenKind::Semicolon,
                        "';' after mangler rule")) {
                return std::nullopt;
            }
            if (!rules.emplace(
                    rule_name, Rule{std::move(*expression), line}).second) {
                fail(line, "duplicate mangler rule '" + rule_name + "'");
                return std::nullopt;
            }
        }
        if (!expect(ModelTokenKind::RightBrace,
                    "'}' after mangling entry")) {
            return std::nullopt;
        }
        for (const auto& [rule_name, rule] : rules) {
            if (rule_name != "entity" && rule_name != "label" &&
                rule_name != "generic") {
                fail(rule.line, "unknown mangler rule '" + rule_name + "'");
                return std::nullopt;
            }
        }
        for (const auto& [helper_name, helper] : helpers) {
            std::unordered_map<std::string, ManglingValueType> variables;
            for (const auto& parameter : helper.parameters) {
                variables.emplace(parameter.name, parameter.type);
            }
            ManglingScope scope;
            scope.variables = &variables;
            scope.helpers = &helpers;
            const auto type =
                validate_mangling_expression(helper.expression, scope);
            if (!type) return std::nullopt;
            if (*type != helper.result_type) {
                fail(helper.line,
                     "mangler helper '" + helper_name +
                         "' body has the wrong result type");
                return std::nullopt;
            }
        }
        const auto take = [&](std::string_view rule_name,
                              ManglingScope scope)
            -> std::optional<ManglingExpression> {
            const auto found = rules.find(std::string(rule_name));
            if (found == rules.end()) {
                fail(entry_line,
                     "missing required mangler rule '" +
                         std::string(rule_name) + "'");
                return std::nullopt;
            }
            scope.helpers = &helpers;
            const auto type =
                validate_mangling_expression(found->second.expression, scope);
            if (!type) return std::nullopt;
            if (*type != ManglingValueType::Text) {
                fail(found->second.line,
                     "mangler rule '" + std::string(rule_name) +
                         "' must produce text");
                return std::nullopt;
            }
            return found->second.expression;
        };

        ManglingEntry result;
        result.canonical_name = std::move(name);
        auto entity = take("entity", {});
        auto label = take("label", {});
        ManglingScope generic_scope;
        generic_scope.entity = true;
        generic_scope.arguments = true;
        auto generic = take("generic", generic_scope);
        if (!entity || !label || !generic || !error_.empty()) {
            return std::nullopt;
        }
        result.entity = std::move(*entity);
        result.label = std::move(*label);
        result.generic = std::move(*generic);
        result.helpers.reserve(helpers.size());
        for (auto& [helper_name, helper] : helpers) {
            (void)helper_name;
            result.helpers.push_back(std::move(helper));
        }
        return result;
    }

    std::optional<ModelValue> parse_value() {
        ModelValue result;
        if (current_.kind == ModelTokenKind::String ||
            current_.kind == ModelTokenKind::Identifier) {
            if (current_.kind == ModelTokenKind::Identifier &&
                (current_.text == "true" || current_.text == "false")) {
                result.kind = ModelValue::Kind::Boolean;
                result.boolean = current_.text == "true";
            } else {
                result.kind = ModelValue::Kind::Text;
                result.text = current_.text;
            }
            advance();
            return result;
        }
        if (current_.kind == ModelTokenKind::Integer) {
            result.kind = ModelValue::Kind::Integer;
            const auto parsed = std::from_chars(
                current_.text.data(),
                current_.text.data() + current_.text.size(),
                result.integer);
            if (parsed.ec != std::errc{}) {
                fail(current_.line, "model integer is out of range");
                return std::nullopt;
            }
            advance();
            return result;
        }
        if (current_.kind == ModelTokenKind::LeftBracket) {
            result.kind = ModelValue::Kind::List;
            advance();
            while (current_.kind != ModelTokenKind::RightBracket &&
                   current_.kind != ModelTokenKind::End) {
                if (current_.kind != ModelTokenKind::String &&
                    current_.kind != ModelTokenKind::Identifier) {
                    fail(current_.line,
                         "model lists contain only strings or identifiers");
                    return std::nullopt;
                }
                result.list.push_back(current_.text);
                advance();
                if (current_.kind == ModelTokenKind::RightBracket) break;
                if (!expect(ModelTokenKind::Comma, "',' between list items")) {
                    return std::nullopt;
                }
            }
            if (!expect(ModelTokenKind::RightBracket, "']' after model list")) {
                return std::nullopt;
            }
            return result;
        }
        if (current_.kind == ModelTokenKind::Invalid) {
            fail(current_.line, "invalid model token '" + current_.text + "'");
        } else {
            fail(current_.line, "expected model property value");
        }
        return std::nullopt;
    }

    bool parse_property(ModelProperties& result) {
        if (current_.kind != ModelTokenKind::Identifier) {
            fail(current_.line, "expected model property name");
            return false;
        }
        const auto name = current_.text;
        const auto line = current_.line;
        advance();
        if (!expect(ModelTokenKind::Equal, "'=' after model property name")) {
            return false;
        }
        auto value = parse_value();
        if (!value) return false;
        if (!expect(ModelTokenKind::Semicolon, "';' after model property")) {
            return false;
        }
        if (!result.emplace(
                name, ModelProperty{std::move(*value), line}).second) {
            fail(line, "duplicate model property '" + name + "'");
            return false;
        }
        return true;
    }

    std::optional<ModelProperties> parse_properties() {
        ModelProperties result;
        while (current_.kind != ModelTokenKind::RightBrace &&
               current_.kind != ModelTokenKind::End) {
            if (!parse_property(result)) return std::nullopt;
        }
        if (!expect(ModelTokenKind::RightBrace, "'}' after model entry")) {
            return std::nullopt;
        }
        return result;
    }

    std::optional<AbiEntry> parse_abi(std::string name,
                                      unsigned entry_line) {
        ModelProperties properties;
        std::vector<AbiNestedBlock> banks;
        std::vector<AbiNestedBlock> rules;
        std::vector<AbiNestedBlock> variadic_states;
        std::vector<AbiNestedBlock> variadic_shadows;
        while (current_.kind != ModelTokenKind::RightBrace &&
               current_.kind != ModelTokenKind::End) {
            if (current_.kind != ModelTokenKind::Identifier) {
                fail(current_.line,
                     "expected ABI property, bank, or rule");
                return std::nullopt;
            }
            if (current_.text != "bank" && current_.text != "rule" &&
                current_.text != "variadic_state" &&
                current_.text != "variadic_shadow") {
                if (!parse_property(properties)) return std::nullopt;
                continue;
            }
            const bool bank = current_.text == "bank";
            const bool rule = current_.text == "rule";
            const bool state = current_.text == "variadic_state";
            const auto line = current_.line;
            advance();
            if (current_.kind != ModelTokenKind::String) {
                fail(current_.line,
                     "expected quoted ABI nested-block name");
                return std::nullopt;
            }
            auto block_name = current_.text;
            advance();
            if (!expect(ModelTokenKind::LeftBrace,
                        "'{' after ABI nested-block name")) {
                return std::nullopt;
            }
            auto block_properties = parse_properties();
            if (!block_properties) return std::nullopt;
            auto& destination = bank ? banks
                              : rule ? rules
                              : state ? variadic_states
                                      : variadic_shadows;
            destination.push_back(
                {std::move(block_name), std::move(*block_properties), line});
        }
        if (!expect(ModelTokenKind::RightBrace, "'}' after ABI entry")) {
            return std::nullopt;
        }
        return make_abi(std::move(name), properties, banks, rules,
                        variadic_states, variadic_shadows, entry_line);
    }

    bool known_properties(
        const ModelProperties& properties,
        std::initializer_list<std::string_view> names) {
        for (const auto& [name, property] : properties) {
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                fail(property.line, "unknown model property '" + name + "'");
                return false;
            }
        }
        return true;
    }

    std::optional<std::string> text_property(
        const ModelProperties& properties, std::string_view name,
        bool required = false) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) {
            if (required) {
                fail(current_.line,
                     "missing required model property '" + std::string(name) + "'");
            }
            return std::nullopt;
        }
        if (found->second.value.kind != ModelValue::Kind::Text) {
            fail(found->second.line,
                 "model property '" + std::string(name) + "' must be text");
            return std::nullopt;
        }
        return found->second.value.text;
    }

    std::optional<std::vector<std::string>> list_property(
        const ModelProperties& properties, std::string_view name) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) return std::nullopt;
        if (found->second.value.kind != ModelValue::Kind::List) {
            fail(found->second.line,
                 "model property '" + std::string(name) + "' must be a list");
            return std::nullopt;
        }
        return found->second.value.list;
    }

    std::optional<bool> bool_property(
        const ModelProperties& properties, std::string_view name) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) return std::nullopt;
        if (found->second.value.kind != ModelValue::Kind::Boolean) {
            fail(found->second.line,
                 "model property '" + std::string(name) + "' must be boolean");
            return std::nullopt;
        }
        return found->second.value.boolean;
    }

    std::optional<unsigned> unsigned_property(
        const ModelProperties& properties, std::string_view name) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) return std::nullopt;
        if (found->second.value.kind != ModelValue::Kind::Integer ||
            found->second.value.integer > static_cast<std::uint64_t>(
                                                std::numeric_limits<unsigned>::max())) {
            fail(found->second.line,
                 "model property '" + std::string(name) +
                     "' must be an unsigned integer");
            return std::nullopt;
        }
        return static_cast<unsigned>(found->second.value.integer);
    }

    std::optional<AbiRegisterFailure> abi_failure(
        std::string_view spelling, unsigned line) {
        if (spelling == "stack") return AbiRegisterFailure::Stack;
        if (spelling == "partial") return AbiRegisterFailure::Partial;
        if (spelling == "error") return AbiRegisterFailure::Error;
        fail(line,
             "ABI register failure must be 'stack', 'partial', or 'error'");
        return std::nullopt;
    }

    std::optional<AbiValueKind> abi_value_kind(
        std::string_view spelling, unsigned line) {
        static constexpr std::pair<std::string_view, AbiValueKind> kinds[] = {
            {"any", AbiValueKind::Any},
            {"integer", AbiValueKind::Integer},
            {"floating", AbiValueKind::Floating},
            {"pointer", AbiValueKind::Pointer},
            {"pair", AbiValueKind::Pair},
            {"aggregate", AbiValueKind::Aggregate},
            {"array", AbiValueKind::Array},
            {"vector", AbiValueKind::Vector},
            {"zero", AbiValueKind::Zero},
        };
        const auto found = std::find_if(
            std::begin(kinds), std::end(kinds),
            [&](const auto& item) { return item.first == spelling; });
        if (found != std::end(kinds)) return found->second;
        fail(line, "unknown ABI value kind '" + std::string(spelling) + "'");
        return std::nullopt;
    }

    std::optional<AbiRuleAction> abi_rule_action(
        std::string_view spelling, unsigned line) {
        static constexpr std::pair<std::string_view, AbiRuleAction>
            actions[] = {
                {"direct", AbiRuleAction::Direct},
                {"split", AbiRuleAction::Split},
                {"flatten", AbiRuleAction::Flatten},
                {"coerce", AbiRuleAction::Coerce},
                {"indirect", AbiRuleAction::Indirect},
                {"stack", AbiRuleAction::Stack},
                {"ignore", AbiRuleAction::Ignore},
            };
        const auto found = std::find_if(
            std::begin(actions), std::end(actions),
            [&](const auto& item) { return item.first == spelling; });
        if (found != std::end(actions)) return found->second;
        fail(line, "unknown ABI rule action '" + std::string(spelling) + "'");
        return std::nullopt;
    }

    std::optional<AbiEntry> make_abi(
        std::string name, const ModelProperties& properties,
        const std::vector<AbiNestedBlock>& bank_blocks,
        const std::vector<AbiNestedBlock>& rule_blocks,
        const std::vector<AbiNestedBlock>& variadic_state_blocks,
        const std::vector<AbiNestedBlock>& variadic_shadow_blocks,
        unsigned line) {
        if (!known_properties(
                properties,
                {"architecture", "address_bits", "aliases",
                 "llvm_calling_convention",
                 "compilation_selectable", "function_selectable",
                 "call_clobbers", "argument_register_failure",
                 "result_register_failure", "stack_layout",
                 "argument_stack_base", "stack_alignment",
                 "stack_slot_bytes", "return_address_bytes",
                 "stack_order", "variadic_supported",
                 "variadic_count_cursor", "variadic_count_register",
                 "variadic_count_bits", "variadic_save_banks",
                 "variadic_save_alignment", "variadic_home_bank",
                 "variadic_home_base", "variadic_home_stride",
                 "variadic_va_list_bytes",
                 "variadic_va_list_alignment"})) {
            return std::nullopt;
        }
        AbiEntry result;
        result.canonical_name = std::move(name);
        const auto architecture =
            text_property(properties, "architecture", true);
        if (!architecture || !error_.empty()) return std::nullopt;
        result.architecture = *architecture;
        const auto address_bits =
            unsigned_property(properties, "address_bits");
        if (!address_bits && error_.empty()) {
            fail(line, "missing required ABI property 'address_bits'");
        }
        if (!address_bits || *address_bits == 0 ||
            *address_bits > std::numeric_limits<std::uint16_t>::max() ||
            !error_.empty()) {
            if (address_bits && *address_bits == 0 && error_.empty()) {
                fail(line, "ABI address_bits must be nonzero");
            } else if (address_bits &&
                       *address_bits >
                           std::numeric_limits<std::uint16_t>::max() &&
                       error_.empty()) {
                fail(line, "ABI address_bits exceeds the supported range");
            }
            return std::nullopt;
        }
        result.address_bits = *address_bits;
        if (const auto llvm =
                text_property(properties, "llvm_calling_convention")) {
            result.llvm_calling_convention = *llvm;
        }
        if (const auto value = list_property(properties, "aliases")) {
            result.aliases = *value;
        }
        if (const auto value =
                bool_property(properties, "compilation_selectable")) {
            result.compilation_selectable = *value;
        }
        if (const auto value =
                bool_property(properties, "function_selectable")) {
            result.function_selectable = *value;
        }
        if (const auto value = list_property(properties, "call_clobbers")) {
            result.call_clobbers = *value;
        }
        if (const auto value =
                text_property(properties, "argument_register_failure")) {
            const auto parsed = abi_failure(*value, line);
            if (!parsed) return std::nullopt;
            result.argument_register_failure = *parsed;
        }
        if (const auto value =
                text_property(properties, "result_register_failure")) {
            const auto parsed = abi_failure(*value, line);
            if (!parsed) return std::nullopt;
            result.result_register_failure = *parsed;
        }
        if (const auto value = text_property(properties, "stack_layout")) {
            if (*value == "packed") {
                result.stack_layout = AbiStackLayout::Packed;
            } else if (*value == "slots") {
                result.stack_layout = AbiStackLayout::Slots;
            } else {
                fail(line, "ABI stack_layout must be 'packed' or 'slots'");
                return std::nullopt;
            }
        }
        if (const auto value =
                unsigned_property(properties, "argument_stack_base")) {
            result.argument_stack_base = *value;
        }
        if (const auto value =
                unsigned_property(properties, "stack_alignment")) {
            result.stack_alignment = *value;
        }
        if (const auto value =
                unsigned_property(properties, "stack_slot_bytes")) {
            result.stack_slot_bytes = *value;
        }
        if (const auto value =
                unsigned_property(properties, "return_address_bytes")) {
            result.return_address_bytes = *value;
        }
        if (const auto value = list_property(properties, "stack_order")) {
            result.stack_order.clear();
            std::unordered_set<std::string_view> seen;
            for (const auto& region : *value) {
                if (!seen.insert(region).second) {
                    fail(line, "ABI stack_order contains duplicate region '" +
                                   region + "'");
                    return std::nullopt;
                }
                if (region == "arguments") {
                    result.stack_order.push_back(
                        AbiStackRegion::Arguments);
                } else if (region == "results") {
                    result.stack_order.push_back(
                        AbiStackRegion::Results);
                } else if (region == "argument_spills") {
                    result.stack_order.push_back(
                        AbiStackRegion::ArgumentSpills);
                } else {
                    fail(line,
                         "ABI stack_order accepts only 'arguments', "
                         "'results', and 'argument_spills'");
                    return std::nullopt;
                }
            }
            if (result.stack_order.empty()) {
                fail(line, "ABI stack_order must not be empty");
                return std::nullopt;
            }
        }
        if (const auto value =
                bool_property(properties, "variadic_supported")) {
            result.variadic_supported = *value;
        }
        result.variadic_count_cursor =
            text_property(properties, "variadic_count_cursor")
                .value_or("");
        result.variadic_count_register =
            text_property(properties, "variadic_count_register")
                .value_or("");
        if (const auto value =
                unsigned_property(properties, "variadic_count_bits")) {
            result.variadic_count_bits = *value;
        }
        if (const auto value =
                list_property(properties, "variadic_save_banks")) {
            result.variadic_save_banks = *value;
        }
        if (const auto value =
                unsigned_property(properties, "variadic_save_alignment")) {
            result.variadic_save_alignment = *value;
        }
        result.variadic_home_bank =
            text_property(properties, "variadic_home_bank").value_or("");
        if (const auto value =
                unsigned_property(properties, "variadic_home_base")) {
            result.variadic_home_base = *value;
        }
        if (const auto value =
                unsigned_property(properties, "variadic_home_stride")) {
            result.variadic_home_stride = *value;
        }
        if (const auto value =
                unsigned_property(properties, "variadic_va_list_bytes")) {
            result.variadic_va_list_bytes = *value;
        }
        if (const auto value = unsigned_property(
                properties, "variadic_va_list_alignment")) {
            result.variadic_va_list_alignment = *value;
        }

        std::unordered_set<std::string> bank_names;
        for (const auto& block : bank_blocks) {
            if (!bank_names.insert(block.name).second) {
                fail(block.line,
                     "duplicate ABI register bank '" + block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(
                    block.properties,
                    {"class", "cursor", "arguments", "results",
                     "register_bits"})) {
                return std::nullopt;
            }
            AbiRegisterBank bank;
            bank.canonical_name = block.name;
            const auto register_class =
                text_property(block.properties, "class", true);
            const auto register_bits =
                unsigned_property(block.properties, "register_bits");
            if (!register_bits && error_.empty()) {
                fail(block.line,
                     "missing required ABI bank property 'register_bits'");
            }
            if (!register_class || !register_bits || *register_bits == 0 ||
                !error_.empty()) {
                if (register_bits && *register_bits == 0 &&
                    error_.empty()) {
                    fail(block.line,
                         "ABI bank register_bits must be nonzero");
                }
                return std::nullopt;
            }
            bank.register_class = *register_class;
            bank.register_bits = *register_bits;
            bank.cursor =
                text_property(block.properties, "cursor")
                    .value_or(bank.canonical_name);
            if (bank.cursor.empty()) {
                fail(block.line, "ABI bank cursor must not be empty");
                return std::nullopt;
            }
            if (const auto value =
                    list_property(block.properties, "arguments")) {
                bank.arguments = *value;
            }
            if (const auto value =
                    list_property(block.properties, "results")) {
                bank.results = *value;
            }
            result.banks.push_back(std::move(bank));
        }

        std::unordered_set<std::string> rule_names;
        for (const auto& block : rule_blocks) {
            if (!rule_names.insert(block.name).second) {
                fail(block.line, "duplicate ABI rule '" + block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(
                    block.properties,
                    {"match", "action", "bank", "min_bits", "max_bits",
                     "unit_bits", "carrier_bits", "max_elements",
                     "stack_alignment", "stack_size", "applies_to",
                     "register_failure", "requires_features",
                     "forbids_features", "merge_banks",
                     "require_natural_alignment"})) {
                return std::nullopt;
            }
            AbiRule rule;
            rule.canonical_name = block.name;
            const auto matches =
                list_property(block.properties, "match");
            const auto action =
                text_property(block.properties, "action", true);
            if (!matches && error_.empty()) {
                fail(block.line,
                     "missing required ABI rule property 'match'");
            }
            if (!matches || matches->empty() || !action ||
                !error_.empty()) {
                if (matches && matches->empty() && error_.empty()) {
                    fail(block.line,
                         "ABI rule match list must not be empty");
                }
                return std::nullopt;
            }
            for (const auto& spelling : *matches) {
                const auto parsed =
                    abi_value_kind(spelling, block.line);
                if (!parsed) return std::nullopt;
                rule.matches.push_back(*parsed);
            }
            const auto parsed_action =
                abi_rule_action(*action, block.line);
            if (!parsed_action) return std::nullopt;
            rule.action = *parsed_action;
            rule.bank =
                text_property(block.properties, "bank").value_or("");
            if (const auto value =
                    unsigned_property(block.properties, "min_bits")) {
                rule.min_bits = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "max_bits")) {
                rule.max_bits = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "unit_bits")) {
                rule.unit_bits = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "carrier_bits")) {
                rule.carrier_bits = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "max_elements")) {
                rule.max_elements = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "stack_alignment")) {
                rule.stack_alignment = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "stack_size")) {
                rule.stack_size = *value;
            }
            if (const auto value =
                    list_property(block.properties, "merge_banks")) {
                rule.merge_banks = *value;
            }
            if (const auto value = bool_property(
                    block.properties, "require_natural_alignment")) {
                rule.require_natural_alignment = *value;
            }
            if (const auto value =
                    list_property(block.properties,
                                  "requires_features")) {
                rule.required_features = *value;
            }
            if (const auto value =
                    list_property(block.properties,
                                  "forbids_features")) {
                rule.forbidden_features = *value;
            }
            if (rule.stack_alignment != 0 &&
                (rule.stack_alignment & (rule.stack_alignment - 1U)) != 0) {
                fail(block.line,
                     "ABI rule stack_alignment must be a power of two");
                return std::nullopt;
            }
            if (const auto applies =
                    list_property(block.properties, "applies_to")) {
                rule.arguments = false;
                rule.fixed_arguments = false;
                rule.variadic_arguments = false;
                rule.results = false;
                for (const auto& direction : *applies) {
                    if (direction == "arguments") {
                        rule.arguments = true;
                        rule.fixed_arguments = true;
                        rule.variadic_arguments = true;
                    } else if (direction == "fixed_arguments") {
                        rule.arguments = true;
                        rule.fixed_arguments = true;
                    } else if (direction == "variadic_arguments") {
                        rule.arguments = true;
                        rule.variadic_arguments = true;
                    } else if (direction == "results") {
                        rule.results = true;
                    } else {
                        fail(block.line,
                             "ABI rule applies_to accepts only 'arguments', "
                             "'fixed_arguments', 'variadic_arguments', and "
                             "'results'");
                        return std::nullopt;
                    }
                }
                if (!rule.arguments && !rule.results) {
                    fail(block.line,
                         "ABI rule applies_to must not be empty");
                    return std::nullopt;
                }
            }
            if (const auto failure =
                    text_property(block.properties, "register_failure")) {
                const auto parsed =
                    abi_failure(*failure, block.line);
                if (!parsed) return std::nullopt;
                rule.failure_override = true;
                rule.failure = *parsed;
            }
            result.rules.push_back(std::move(rule));
        }

        std::unordered_set<std::string> variadic_state_names;
        for (const auto& block : variadic_state_blocks) {
            if (!variadic_state_names.insert(block.name).second) {
                fail(block.line, "duplicate ABI variadic state '" +
                                     block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(
                    block.properties,
                    {"type", "kind", "cursor", "base", "stride", "alignment",
                     "llvm_va_list_offset"})) {
                return std::nullopt;
            }
            AbiVariadicState state;
            state.canonical_name = block.name;
            const auto type = text_property(block.properties, "type", true);
            const auto kind = text_property(block.properties, "kind", true);
            if (!type || type->empty() || !kind || !error_.empty()) {
                if (type && type->empty() && error_.empty()) {
                    fail(block.line,
                         "ABI variadic-state type must not be empty");
                }
                return std::nullopt;
            }
            state.type = *type;
            if (*kind == "cursor_offset") {
                state.kind = AbiVariadicStateKind::CursorOffset;
            } else if (*kind == "stack_address") {
                state.kind = AbiVariadicStateKind::StackAddress;
            } else if (*kind == "cursor_address") {
                state.kind = AbiVariadicStateKind::CursorAddress;
            } else if (*kind == "register_save_address") {
                state.kind = AbiVariadicStateKind::RegisterSaveAddress;
            } else {
                fail(block.line,
                     "ABI variadic-state kind must be 'cursor_offset', "
                     "'stack_address', 'cursor_address', or "
                     "'register_save_address'");
                return std::nullopt;
            }
            state.cursor =
                text_property(block.properties, "cursor").value_or("");
            if (const auto value =
                    unsigned_property(block.properties, "base")) {
                state.base = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "stride")) {
                state.stride = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "alignment")) {
                state.alignment = *value;
            }
            state.llvm_va_list_offset = unsigned_property(
                block.properties, "llvm_va_list_offset");
            const bool needs_cursor =
                state.kind == AbiVariadicStateKind::CursorOffset ||
                state.kind == AbiVariadicStateKind::CursorAddress;
            if (needs_cursor &&
                (state.cursor.empty() || state.stride == 0)) {
                fail(block.line,
                     "cursor-based ABI variadic state requires a nonempty "
                     "cursor and nonzero stride");
                return std::nullopt;
            }
            if (state.alignment == 0 ||
                (state.alignment & (state.alignment - 1U)) != 0) {
                fail(block.line,
                     "ABI variadic-state alignment must be a nonzero power of two");
                return std::nullopt;
            }
            result.variadic_states.push_back(std::move(state));
        }

        std::unordered_set<std::string> variadic_shadow_names;
        for (const auto& block : variadic_shadow_blocks) {
            if (!variadic_shadow_names.insert(block.name).second) {
                fail(block.line, "duplicate ABI variadic shadow '" +
                                     block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(
                    block.properties,
                    {"source_bank", "target_bank", "fixed_arguments",
                     "unnamed_arguments"})) {
                return std::nullopt;
            }
            AbiVariadicShadow shadow;
            shadow.canonical_name = block.name;
            const auto source =
                text_property(block.properties, "source_bank", true);
            const auto target =
                text_property(block.properties, "target_bank", true);
            if (!source || source->empty() || !target || target->empty() ||
                !error_.empty()) {
                fail(block.line,
                     "ABI variadic shadow requires nonempty source and "
                     "target banks");
                return std::nullopt;
            }
            shadow.source_bank = *source;
            shadow.target_bank = *target;
            if (const auto value = bool_property(
                    block.properties, "fixed_arguments")) {
                shadow.fixed_arguments = *value;
            }
            if (const auto value = bool_property(
                    block.properties, "unnamed_arguments")) {
                shadow.unnamed_arguments = *value;
            }
            if (!shadow.fixed_arguments && !shadow.unnamed_arguments) {
                fail(block.line,
                     "ABI variadic shadow must apply to fixed or unnamed "
                     "arguments");
                return std::nullopt;
            }
            result.variadic_shadows.push_back(std::move(shadow));
        }

        if (!error_.empty()) return std::nullopt;
        const auto power_of_two = [](unsigned value) {
            return value != 0 && (value & (value - 1U)) == 0;
        };
        if (!power_of_two(result.stack_alignment) ||
            !power_of_two(result.stack_slot_bytes)) {
            fail(line, "ABI stack_alignment and stack_slot_bytes must be "
                       "nonzero powers of two");
            return std::nullopt;
        }
        if (result.banks.empty() || result.rules.empty()) {
            fail(line, "ABI entries require at least one bank and one rule");
            return std::nullopt;
        }
        if (!power_of_two(result.variadic_save_alignment) ||
            !power_of_two(result.variadic_va_list_alignment)) {
            fail(line,
                 "ABI variadic save/va-list alignments must be nonzero "
                 "powers of two");
            return std::nullopt;
        }
        if ((!result.variadic_count_cursor.empty()) !=
                (!result.variadic_count_register.empty()) ||
            (!result.variadic_count_cursor.empty() &&
             result.variadic_count_bits == 0)) {
            fail(line,
                 "ABI variadic count requires cursor, register, and nonzero "
                 "bit width together");
            return std::nullopt;
        }
        if (!result.variadic_home_bank.empty() &&
            result.variadic_home_stride == 0) {
            fail(line,
                 "ABI variadic home bank requires a nonzero stride");
            return std::nullopt;
        }
        if (!result.variadic_supported &&
            (!result.variadic_states.empty() ||
             !result.variadic_shadows.empty() ||
             !result.variadic_count_cursor.empty() ||
             !result.variadic_save_banks.empty() ||
             !result.variadic_home_bank.empty())) {
            fail(line,
                 "ABI variadic policy requires variadic_supported = true");
            return std::nullopt;
        }
        return result;
    }

    std::optional<OptionValue> option_value(
        const std::string& name, const ModelProperty& property) {
        switch (property.value.kind) {
        case ModelValue::Kind::Boolean: return property.value.boolean;
        case ModelValue::Kind::Integer: return property.value.integer;
        case ModelValue::Kind::Text: return property.value.text;
        case ModelValue::Kind::List:
            fail(property.line,
                 "compiler option property '" + name +
                     "' cannot have a list value");
            return std::nullopt;
        }
        return std::nullopt;
    }

    bool collect_option_properties(
        const ModelProperties& properties,
        std::initializer_list<std::string_view> reserved,
        std::vector<OptionAssignment>& result) {
        for (const auto& [name, property] : properties) {
            if (std::find(reserved.begin(), reserved.end(), name) !=
                reserved.end()) {
                continue;
            }
            if (!name.starts_with("f.") && !name.starts_with("m.")) {
                fail(property.line, "unknown model property '" + name + "'");
                return false;
            }
            auto value = option_value(name, property);
            if (!value) return false;
            result.push_back(
                {name, std::move(*value),
                 origin_ + ':' + std::to_string(property.line)});
        }
        std::sort(result.begin(), result.end(),
                  [](const OptionAssignment& left,
                     const OptionAssignment& right) {
                      return left.name < right.name;
                  });
        return true;
    }

    std::optional<OptimizationEntry> make_optimization(
        std::string name, const ModelProperties& properties, unsigned line) {
        OptimizationEntry result;
        result.canonical_name = std::move(name);
        result.inherits = text_property(properties, "inherits");
        if (const auto targets = list_property(properties, "targets")) {
            result.targets = *targets;
        }
        if (!collect_option_properties(
                properties, {"inherits", "targets"}, result.options)) {
            return std::nullopt;
        }
        if (result.targets.empty() &&
            std::any_of(result.options.begin(), result.options.end(),
                        [](const OptionAssignment& option) {
                            return option.name.starts_with("m.");
                        })) {
            fail(line,
                 "optimization entry with m.* options requires a targets "
                 "restriction");
            return std::nullopt;
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    std::optional<ProfileEntry> make_profile(
        std::string name, const ModelProperties& properties, unsigned) {
        ProfileEntry result;
        result.canonical_name = std::move(name);
        if (const auto value = list_property(properties, "default_for")) {
            result.default_for = *value;
        }
        result.target = text_property(properties, "target");
        result.abi = text_property(properties, "abi");
        result.mangling = text_property(properties, "mangling");
        result.optimization = text_property(properties, "optimization");
        if (!collect_option_properties(
                properties,
                {"default_for", "target", "abi", "mangling",
                 "optimization"}, result.options)) {
            return std::nullopt;
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    ModelLexer lexer_;
    std::string origin_;
    ModelToken current_;
    std::string error_;
};

std::string normalized(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return (error ? path.lexically_normal() : canonical).generic_string();
}

bool glob_matches(std::string_view pattern, std::string_view text) {
    std::size_t pattern_index{};
    std::size_t text_index{};
    std::size_t star = std::string_view::npos;
    std::size_t retry{};
    while (text_index < text.size()) {
        if (pattern_index < pattern.size() &&
            pattern[pattern_index] == text[text_index]) {
            ++pattern_index;
            ++text_index;
        } else if (pattern_index < pattern.size() &&
                   pattern[pattern_index] == '*') {
            star = pattern_index++;
            retry = text_index;
        } else if (star != std::string_view::npos) {
            pattern_index = star + 1;
            text_index = ++retry;
        } else {
            return false;
        }
    }
    while (pattern_index < pattern.size() && pattern[pattern_index] == '*') {
        ++pattern_index;
    }
    return pattern_index == pattern.size();
}

bool validate_abi_model(const TargetInfo& target, const AbiEntry& abi,
                        Diagnostics& diagnostics) {
    const auto registers_valid = [&](const AbiRegisterBank& bank,
                                     const std::vector<std::string>& names,
                                     std::string_view property) {
        for (const auto& name : names) {
            const auto* entry = find_register(target, name);
            if (!entry || entry->register_class != bank.register_class ||
                entry->bits < bank.register_bits) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' bank '" +
                    bank.canonical_name + "' property '" +
                    std::string(property) +
                    "' contains an incompatible register '" + name + "'");
                return false;
            }
        }
        return true;
    };
    std::unordered_set<std::string_view> bank_names;
    std::unordered_set<std::string_view> slot_cursors;
    const auto has_stack_region = [&](AbiStackRegion sought) {
        return std::find(abi.stack_order.begin(), abi.stack_order.end(),
                         sought) != abi.stack_order.end();
    };
    bool needs_argument_stack{};
    bool needs_result_stack{};
    for (const auto& bank : abi.banks) {
        if (!bank_names.insert(bank.canonical_name).second) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' contains duplicate register bank '" +
                bank.canonical_name + "'");
            return false;
        }
        if (!registers_valid(bank, bank.arguments, "arguments") ||
            !registers_valid(bank, bank.results, "results")) {
            return false;
        }
    }
    for (const auto& clobber : abi.call_clobbers) {
        if (clobber != "memory" && clobber != "flags" &&
            !find_register(target, clobber)) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' contains unknown call clobber '" + clobber + "'");
            return false;
        }
    }
    for (const auto& rule : abi.rules) {
        std::unordered_set<std::string_view> required_features;
        for (const auto& feature : rule.required_features) {
            if (feature.empty() ||
                !required_features.insert(feature).second) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' rule '" +
                    rule.canonical_name +
                    "' contains an empty or duplicate required feature");
                return false;
            }
        }
        std::unordered_set<std::string_view> forbidden_features;
        for (const auto& feature : rule.forbidden_features) {
            if (feature.empty() ||
                !forbidden_features.insert(feature).second ||
                required_features.contains(feature)) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' rule '" +
                    rule.canonical_name +
                    "' contains an empty, duplicate, or contradictory "
                    "forbidden feature");
                return false;
            }
        }
        const bool needs_bank =
            rule.action == AbiRuleAction::Direct ||
            rule.action == AbiRuleAction::Split ||
            rule.action == AbiRuleAction::Coerce ||
            rule.action == AbiRuleAction::Indirect;
        const auto rule_failure = [&](bool arguments) {
            if (rule.failure_override) return rule.failure;
            return arguments ? abi.argument_register_failure
                             : abi.result_register_failure;
        };
        if (rule.arguments &&
            (rule.action == AbiRuleAction::Stack ||
             (needs_bank &&
              rule_failure(true) != AbiRegisterFailure::Error))) {
            needs_argument_stack = true;
        }
        if (rule.results &&
            (rule.action == AbiRuleAction::Stack ||
             (needs_bank && rule.action != AbiRuleAction::Indirect &&
              rule_failure(false) != AbiRegisterFailure::Error))) {
            needs_result_stack = true;
        }
        const auto bank = std::find_if(
            abi.banks.begin(), abi.banks.end(),
            [&](const AbiRegisterBank& candidate) {
                return candidate.canonical_name == rule.bank;
            });
        if (needs_bank && bank == abi.banks.end()) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name + "' names unknown bank '" +
                rule.bank + "'");
            return false;
        }
        if (!needs_bank && !rule.bank.empty()) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' supplies a bank to an action that does not use one");
            return false;
        }
        if (needs_bank) {
            if (rule.arguments && bank->arguments.empty()) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' rule '" +
                    rule.canonical_name +
                    "' applies to arguments but its bank has no argument "
                    "registers");
                return false;
            }
            const auto& result_registers =
                rule.action == AbiRuleAction::Indirect
                    ? bank->arguments
                    : bank->results;
            if (rule.results && result_registers.empty()) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' rule '" +
                    rule.canonical_name +
                    "' applies to results but its bank has no compatible "
                    "result channel");
                return false;
            }
            if (abi.stack_layout == AbiStackLayout::Slots &&
                rule.arguments) {
                slot_cursors.insert(bank->cursor);
            }
        }
        if (rule.max_bits != 0 && rule.min_bits > rule.max_bits) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' has min_bits greater than max_bits");
            return false;
        }
        if ((rule.action == AbiRuleAction::Split ||
             rule.action == AbiRuleAction::Coerce) &&
            rule.unit_bits == 0 &&
            (bank == abi.banks.end() || bank->register_bits == 0)) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' requires a nonzero unit_bits or bank register_bits");
            return false;
        }
        if ((!rule.merge_banks.empty() ||
             rule.require_natural_alignment) &&
            rule.action != AbiRuleAction::Flatten) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' uses flatten-only merge/alignment properties");
            return false;
        }
        if (!rule.merge_banks.empty() && rule.unit_bits == 0) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' requires nonzero unit_bits when merge_banks is present");
            return false;
        }
        std::unordered_set<std::string_view> merge_names;
        for (const auto& merge_name : rule.merge_banks) {
            const auto merge_bank = std::find_if(
                abi.banks.begin(), abi.banks.end(),
                [&](const AbiRegisterBank& candidate) {
                    return candidate.canonical_name == merge_name;
                });
            if (merge_name.empty() || merge_bank == abi.banks.end() ||
                !merge_names.insert(merge_name).second) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' rule '" +
                    rule.canonical_name +
                    "' has an empty, duplicate, or unknown merge bank");
                return false;
            }
            if (merge_bank->register_bits < rule.unit_bits) {
                diagnostics.command_error(
                    "ABI model '" + abi.canonical_name + "' rule '" +
                    rule.canonical_name +
                    "' has a merge bank narrower than unit_bits");
                return false;
            }
        }
        if (bank != abi.banks.end() &&
            (rule.action == AbiRuleAction::Split ||
             rule.action == AbiRuleAction::Coerce) &&
            rule.unit_bits > bank->register_bits) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' has unit_bits wider than its register bank");
            return false;
        }
        if (bank != abi.banks.end() &&
            rule.carrier_bits > bank->register_bits) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' has carrier_bits wider than its register bank");
            return false;
        }
        if (bank != abi.banks.end() &&
            rule.action == AbiRuleAction::Indirect &&
            bank->register_bits < abi.address_bits) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' uses an indirect bank narrower than address_bits");
            return false;
        }
        if (rule.action == AbiRuleAction::Indirect &&
            rule.unit_bits != 0 && rule.unit_bits % 8U != 0) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name + "' rule '" +
                rule.canonical_name +
                "' indirect unit_bits must be byte-addressable");
            return false;
        }
    }
    if (abi.stack_layout == AbiStackLayout::Slots &&
        slot_cursors.size() > 1) {
        diagnostics.command_error(
            "ABI model '" + abi.canonical_name +
            "' uses positional stack slots but its argument banks do not "
            "share one cursor");
        return false;
    }
    if (needs_argument_stack &&
        !has_stack_region(AbiStackRegion::Arguments)) {
        diagnostics.command_error(
            "ABI model '" + abi.canonical_name +
            "' can place arguments on the stack but stack_order omits "
            "'arguments'");
        return false;
    }
    if (needs_result_stack &&
        !has_stack_region(AbiStackRegion::Results)) {
        diagnostics.command_error(
            "ABI model '" + abi.canonical_name +
            "' can place results on the stack but stack_order omits "
            "'results'");
        return false;
    }
    const auto bank_named = [&](std::string_view name)
        -> const AbiRegisterBank* {
        const auto found = std::find_if(
            abi.banks.begin(), abi.banks.end(),
            [&](const AbiRegisterBank& bank) {
                return bank.canonical_name == name;
            });
        return found == abi.banks.end() ? nullptr : &*found;
    };
    const auto cursor_known = [&](std::string_view cursor) {
        return std::any_of(
            abi.banks.begin(), abi.banks.end(),
            [&](const AbiRegisterBank& bank) {
                return bank.cursor == cursor;
            });
    };
    if (!abi.variadic_count_cursor.empty()) {
        const auto* count_register =
            find_register(target, abi.variadic_count_register);
        if (!cursor_known(abi.variadic_count_cursor) || !count_register ||
            count_register->bits < abi.variadic_count_bits) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' has an invalid variadic count cursor/register");
            return false;
        }
    }
    for (const auto& shadow : abi.variadic_shadows) {
        const auto* source = bank_named(shadow.source_bank);
        const auto* destination = bank_named(shadow.target_bank);
        if (!source || !destination ||
            source->cursor != destination->cursor ||
            destination->arguments.size() < source->arguments.size()) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' variadic shadow '" + shadow.canonical_name +
                "' requires compatible banks sharing one cursor");
            return false;
        }
    }
    for (const auto& bank_name : abi.variadic_save_banks) {
        const auto* bank = bank_named(bank_name);
        if (!bank || bank->arguments.empty()) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' variadic_save_banks contains unknown or empty bank '" +
                bank_name + "'");
            return false;
        }
    }
    if (!abi.variadic_home_bank.empty()) {
        const auto* bank = bank_named(abi.variadic_home_bank);
        if (!bank || bank->arguments.empty()) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' has an invalid variadic home bank");
            return false;
        }
    }
    for (const auto& state : abi.variadic_states) {
        const bool needs_cursor =
            state.kind == AbiVariadicStateKind::CursorOffset ||
            state.kind == AbiVariadicStateKind::CursorAddress;
        if ((needs_cursor && !cursor_known(state.cursor)) ||
            (state.kind == AbiVariadicStateKind::RegisterSaveAddress &&
             abi.variadic_save_banks.empty()) ||
            (state.llvm_va_list_offset &&
             (*state.llvm_va_list_offset >= abi.variadic_va_list_bytes ||
              abi.variadic_va_list_bytes == 0))) {
            diagnostics.command_error(
                "ABI model '" + abi.canonical_name +
                "' has invalid variadic state '" +
                state.canonical_name + "'");
            return false;
        }
    }
    return true;
}

} // namespace

ModelRegistry::ModelRegistry() {
    for (const auto& source : shipped_model_sources()) {
        if (!load_text(source.text, std::string(source.name), nullptr)) {
            throw std::logic_error("invalid shipped Cross model '" +
                                   std::string(source.name) + "'");
        }
    }
}

bool ModelRegistry::load_text(std::string_view text, std::string origin,
                              Diagnostics* diagnostics) {
    ModelParser parser(text, origin);
    auto document = parser.parse();
    if (!document) {
        if (diagnostics) diagnostics->command_error(parser.error());
        return false;
    }

    std::unordered_set<std::string> abi_names;
    for (const auto& entry : abis_) {
        abi_names.insert(entry.architecture + '\n' + entry.canonical_name);
        for (const auto& alias : entry.aliases) {
            abi_names.insert(entry.architecture + '\n' + alias);
        }
    }
    for (const auto& entry : document->abis) {
        const auto add_name = [&](std::string_view name) {
            return abi_names.insert(entry.architecture + '\n' +
                                    std::string(name)).second;
        };
        if (!add_name(entry.canonical_name)) {
            if (diagnostics) {
                diagnostics->command_error(
                    origin + ": duplicate ABI model name or alias '" +
                    entry.canonical_name + "' for " + entry.architecture);
            }
            return false;
        }
        for (const auto& alias : entry.aliases) {
            if (!add_name(alias)) {
                if (diagnostics) {
                    diagnostics->command_error(
                        origin + ": duplicate ABI model name or alias '" +
                        alias + "' for " + entry.architecture);
                }
                return false;
            }
        }
    }

    std::unordered_set<std::string> mangling_names;
    for (const auto& entry : manglings_) {
        mangling_names.insert(entry.canonical_name);
    }
    for (const auto& entry : document->manglings) {
        if (!mangling_names.insert(entry.canonical_name).second) {
            if (diagnostics) {
                diagnostics->command_error(
                    origin + ": duplicate mangling model '" +
                    entry.canonical_name + "'");
            }
            return false;
        }
    }

    std::unordered_set<std::string> optimization_names;
    for (const auto& entry : optimizations_) {
        optimization_names.insert(entry.canonical_name);
    }
    for (const auto& entry : document->optimizations) {
        if (!optimization_names.insert(entry.canonical_name).second) {
            if (diagnostics) {
                diagnostics->command_error(
                    origin + ": duplicate optimization model '" +
                    entry.canonical_name + "'");
            }
            return false;
        }
    }

    std::unordered_set<std::string> profile_names;
    for (const auto& entry : profiles_) {
        profile_names.insert(entry.canonical_name);
    }
    for (const auto& entry : document->profiles) {
        if (!profile_names.insert(entry.canonical_name).second) {
            if (diagnostics) {
                diagnostics->command_error(
                    origin + ": duplicate profile model '" +
                    entry.canonical_name + "'");
            }
            return false;
        }
    }

    const auto abi_capacity = static_cast<std::size_t>(AbiId::invalid_value);
    if (abis_.size() > abi_capacity ||
        document->abis.size() > abi_capacity - abis_.size()) {
        if (diagnostics) {
            diagnostics->command_error(
                origin + ": ABI model count exceeds the typed registry limit");
        }
        return false;
    }
    for (const auto& entry : document->abis) {
        if (entry.variadic_states.size() >
            static_cast<std::size_t>(AbiStateId::invalid_value)) {
            if (diagnostics) {
                diagnostics->command_error(
                    origin + ": ABI model '" + entry.canonical_name +
                    "' has too many variadic-state entries");
            }
            return false;
        }
    }

    for (auto& entry : document->abis) {
        entry.id = {static_cast<std::uint32_t>(abis_.size())};
        for (std::size_t index = 0; index < entry.variadic_states.size();
             ++index) {
            entry.variadic_states[index].id = {
                static_cast<std::uint16_t>(index)};
        }
        abis_.push_back(std::move(entry));
    }
    for (auto& entry : document->manglings) {
        manglings_.push_back(std::move(entry));
    }
    for (auto& entry : document->optimizations) {
        optimizations_.push_back(std::move(entry));
    }
    for (auto& entry : document->profiles) {
        profiles_.push_back(std::move(entry));
    }
    origins_.push_back(std::move(origin));
    return true;
}

bool ModelRegistry::load_file(const std::filesystem::path& path,
                              Diagnostics& diagnostics) {
    const auto identity = normalized(path);
    if (loaded_files_.contains(identity)) return true;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        diagnostics.command_error("cannot read Cross model file '" +
                                  path.string() + "'");
        return false;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!load_text(contents.str(), identity, &diagnostics)) return false;
    loaded_files_.insert(identity);
    return true;
}

const ProfileEntry* ModelRegistry::default_profile(
    std::string_view triple) const {
    const ProfileEntry* best{};
    std::size_t best_score{};
    bool ambiguous = false;
    for (const auto& profile : profiles_) {
        for (const auto& pattern : profile.default_for) {
            if (!glob_matches(pattern, triple)) continue;
            const auto score = static_cast<std::size_t>(std::count_if(
                pattern.begin(), pattern.end(),
                [](char ch) { return ch != '*'; }));
            if (!best || score > best_score) {
                best = &profile;
                best_score = score;
                ambiguous = false;
            } else if (score == best_score && best != &profile) {
                ambiguous = true;
            }
        }
    }
    return ambiguous ? nullptr : best;
}

std::string_view ModelRegistry::default_abi(
    std::string_view architecture, std::string_view triple) const {
    const auto* profile = default_profile(triple);
    if (!profile || !profile->abi) return {};
    const auto* abi = find_abi(architecture, *profile->abi, triple);
    return abi ? std::string_view(abi->canonical_name) : std::string_view{};
}

const AbiEntry* ModelRegistry::find_abi(
    std::string_view architecture, std::string_view name,
    std::string_view triple) const {
    if (name.empty() || name == "default") {
        name = default_abi(architecture, triple);
    }
    for (const auto& entry : abis_) {
        if (entry.architecture != architecture) continue;
        if (entry.canonical_name == name) return &entry;
        for (const auto& alias : entry.aliases) {
            if (alias == name) return &entry;
        }
    }
    return nullptr;
}

const AbiEntry* ModelRegistry::find_abi(AbiId id) const {
    return id.valid() && id.value < abis_.size() ? &abis_[id.value]
                                                 : nullptr;
}

const ManglingEntry* ModelRegistry::find_mangling(
    std::string_view name) const {
    if (name.empty() || name == "default") {
        return manglings_.empty() ? nullptr : &manglings_.front();
    }
    for (const auto& entry : manglings_) {
        if (entry.canonical_name == name) return &entry;
    }
    return nullptr;
}

const ProfileEntry* ModelRegistry::find_profile(std::string_view name) const {
    for (const auto& entry : profiles_) {
        if (entry.canonical_name == name) return &entry;
    }
    return nullptr;
}

const OptimizationEntry* ModelRegistry::find_optimization(
    std::string_view name) const {
    for (const auto& entry : optimizations_) {
        if (entry.canonical_name == name) return &entry;
    }
    return nullptr;
}

ModelRegistry& model_registry() {
    static ModelRegistry registry;
    return registry;
}

bool configure_models(CompilerOptions& options, Diagnostics& diagnostics) {
    auto& registry = model_registry();
    for (const auto& path : options.model_paths) {
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) {
            diagnostics.command_error(
                "Cross model search path is not a directory: '" +
                path.string() + "'");
            return false;
        }
    }
    for (const auto& file : options.model_files) {
        std::error_code error;
        std::optional<std::filesystem::path> resolved;
        if (std::filesystem::is_regular_file(file, error)) {
            resolved = file;
        } else if (!file.is_absolute()) {
            for (const auto& path : options.model_paths) {
                const auto candidate = path / file;
                error.clear();
                if (std::filesystem::is_regular_file(candidate, error)) {
                    resolved = candidate;
                    break;
                }
            }
        }
        if (!resolved) {
            diagnostics.command_error(
                "cannot find Cross model file '" + file.string() + "'");
            return false;
        }
        if (!registry.load_file(*resolved, diagnostics)) return false;
    }
    for (const auto& abi : registry.abis()) {
        const auto target = std::find_if(
            all_targets().begin(), all_targets().end(),
            [&](const TargetInfo* candidate) {
                return candidate->architecture == abi.architecture;
            });
        if (target != all_targets().end() &&
            !validate_abi_model(**target, abi, diagnostics)) {
            return false;
        }
    }

    const ProfileEntry* profile{};
    if (options.profile_explicit && options.profile != "default") {
        profile = registry.find_profile(options.profile);
        if (!profile) {
            diagnostics.command_error("unknown Cross model profile '" +
                                      options.profile + "'");
            return false;
        }
        if (!options.target_explicit && profile->target) {
            options.target = *profile->target;
        }
    } else {
        profile = registry.default_profile(options.target);
    }
    if (!profile && options.profile_explicit) {
        diagnostics.command_error(
            "no default Cross model profile matches target '" +
            options.target + "'");
        return false;
    }
    if (profile) {
        options.profile = profile->canonical_name;
        if (!options.abi_explicit && profile->abi) options.abi = *profile->abi;
        if (!options.mangling_explicit && profile->mangling) {
            options.mangling = *profile->mangling;
        }
        if (!options.optimization_explicit && profile->optimization) {
            options.optimization = *profile->optimization;
        }
    }

    const auto* selected_target = target_for_triple(options.target);
    const auto* optimization =
        registry.find_optimization(options.optimization);
    if (!optimization) {
        diagnostics.command_error("unknown optimization preset '" +
                                  options.optimization + "'");
        return false;
    }

    std::vector<OptionAssignment> preset_options;
    std::unordered_map<std::string, std::size_t> preset_indices;
    std::unordered_map<std::string, unsigned> visit_state;
    std::vector<std::string> visit_stack;
    std::function<bool(const OptimizationEntry&)> inherit =
        [&](const OptimizationEntry& entry) {
            auto& state = visit_state[entry.canonical_name];
            if (state == 2) return true;
            if (state == 1) {
                std::string cycle;
                for (const auto& name : visit_stack) {
                    if (!cycle.empty()) cycle += " -> ";
                    cycle += name;
                }
                if (!cycle.empty()) cycle += " -> ";
                cycle += entry.canonical_name;
                diagnostics.command_error(
                    "optimization inheritance cycle: " + cycle);
                return false;
            }
            state = 1;
            visit_stack.push_back(entry.canonical_name);
            if (entry.inherits) {
                const auto* parent =
                    registry.find_optimization(*entry.inherits);
                if (!parent) {
                    diagnostics.command_error(
                        "optimization preset '" + entry.canonical_name +
                        "' inherits unknown preset '" + *entry.inherits +
                        "'");
                    return false;
                }
                if (!inherit(*parent)) return false;
            }
            if (!entry.targets.empty()) {
                if (!selected_target ||
                    std::find(entry.targets.begin(), entry.targets.end(),
                              selected_target->architecture) ==
                        entry.targets.end()) {
                    diagnostics.command_error(
                        "optimization preset '" + entry.canonical_name +
                        "' is unavailable for target '" + options.target +
                        "'");
                    return false;
                }
            }
            for (const auto& setting : entry.options) {
                const auto found = preset_indices.find(setting.name);
                if (found == preset_indices.end()) {
                    preset_indices.emplace(setting.name,
                                           preset_options.size());
                    preset_options.push_back(setting);
                } else {
                    preset_options[found->second] = setting;
                }
            }
            visit_stack.pop_back();
            state = 2;
            return true;
        };
    if (!inherit(*optimization)) return false;

    const std::span<const OptionDefinition> target_definitions =
        selected_target
            ? std::span<const OptionDefinition>(selected_target->options)
            : std::span<const OptionDefinition>{};
    const std::span<const OptionAssignment> profile_options =
        profile ? std::span<const OptionAssignment>(profile->options)
                : std::span<const OptionAssignment>{};
    if (!resolve_registered_options(
            options, target_definitions, preset_options, profile_options,
            diagnostics)) {
        return false;
    }
    if (selected_target &&
        !normalize_subtarget_options(
            *selected_target, options, diagnostics)) {
        return false;
    }

    const auto* mangling = registry.find_mangling(options.mangling);
    if (!mangling) {
        diagnostics.command_error("unknown name-mangling model '" +
                                  options.mangling + "'");
        return false;
    }
    options.mangling = mangling->canonical_name;
    if (const auto* target = target_for_triple(options.target)) {
        const auto* abi = registry.find_abi(
            target->architecture, options.abi, options.target);
        if (!abi) {
            diagnostics.command_error("unknown ABI model '" + options.abi +
                                      "' for target '" + options.target + "'");
            return false;
        }
        options.abi = abi->canonical_name;
    }
    return true;
}

namespace {

enum class ManglingRuntimeType { Text, Integer, Boolean };

struct ManglingValue {
    ManglingRuntimeType type{ManglingRuntimeType::Text};
    std::string text;
    std::uint64_t integer{};
    bool boolean{};
};

struct ManglingEvaluationState {
    std::unordered_map<std::string, std::uint64_t> substitutions;
    std::uint64_t next_substitution{};
    bool failed{};
};

struct ManglingEvaluationContext {
    ManglingEntityDescriptor descriptor;
    const ManglingEntry* mangling{};
    ManglingEvaluationState* state{};
    std::string_view entity;
    std::string_view text;
    std::string_view mode;
    std::span<const ManglingArgument> arguments;
    std::uint64_t index{};
    std::uint64_t count{};
    std::optional<std::uint64_t> substitution_index;
    std::unordered_map<std::string, ManglingValue> variables;
    unsigned helper_depth{};
};

std::string ascii_case(std::string text, bool upper) {
    for (char& character : text) {
        if (upper && character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - ('a' - 'A'));
        } else if (!upper && character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return text;
}

std::string render_radix(std::uint64_t value, std::uint64_t radix) {
    if (radix < 2 || radix > 36) return {};
    static constexpr std::string_view digits =
        "0123456789abcdefghijklmnopqrstuvwxyz";
    char buffer[65]{};
    auto* current = std::end(buffer);
    do {
        *--current = digits[static_cast<std::size_t>(value % radix)];
        value /= radix;
    } while (value != 0);
    return {current, std::end(buffer)};
}

ManglingValue evaluate_mangling_expression(
    const ManglingExpression& expression,
    const ManglingEvaluationContext& context) {
    switch (expression.kind) {
    case ManglingExpressionKind::Literal:
        return {.text = expression.literal};
    case ManglingExpressionKind::IntegerLiteral:
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = expression.integer};
    case ManglingExpressionKind::BooleanLiteral:
        return {.type = ManglingRuntimeType::Boolean,
                .text = {},
                .integer = 0,
                .boolean = expression.boolean};
    case ManglingExpressionKind::Name:
        return {.text = std::string(context.descriptor.qualified_name)};
    case ManglingExpressionKind::Entity:
        return {.text = std::string(context.entity)};
    case ManglingExpressionKind::Kind:
        return {.text = std::string(context.descriptor.kind)};
    case ManglingExpressionKind::Result:
        return {.text = std::string(context.descriptor.result)};
    case ManglingExpressionKind::Variadic:
        return {.type = ManglingRuntimeType::Boolean,
                .text = {},
                .integer = 0,
                .boolean = context.descriptor.variadic};
    case ManglingExpressionKind::ParameterCount:
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = static_cast<std::uint64_t>(
                    context.descriptor.parameters.size())};
    case ManglingExpressionKind::Text:
        return {.text = std::string(context.text)};
    case ManglingExpressionKind::Mode:
        return {.text = std::string(context.mode)};
    case ManglingExpressionKind::Index:
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = context.index};
    case ManglingExpressionKind::Count:
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = context.count};
    case ManglingExpressionKind::SubstitutionIndex:
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = context.substitution_index.value_or(0)};
    case ManglingExpressionKind::Variable: {
        const auto found = context.variables.find(expression.literal);
        return found == context.variables.end() ? ManglingValue{}
                                                : found->second;
    }
    case ManglingExpressionKind::HelperCall: {
        if (!context.mangling || !context.state ||
            context.helper_depth >= 128) {
            if (context.state) context.state->failed = true;
            return {};
        }
        const auto helper = std::find_if(
            context.mangling->helpers.begin(),
            context.mangling->helpers.end(),
            [&](const ManglingHelper& candidate) {
                return candidate.name == expression.literal;
            });
        if (helper == context.mangling->helpers.end() ||
            helper->parameters.size() != expression.operands.size()) {
            context.state->failed = true;
            return {};
        }
        std::vector<ManglingValue> arguments;
        arguments.reserve(expression.operands.size());
        for (const auto& operand : expression.operands) {
            arguments.push_back(
                evaluate_mangling_expression(operand, context));
        }
        auto nested = context;
        nested.variables.clear();
        nested.helper_depth = context.helper_depth + 1;
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            nested.variables.emplace(
                helper->parameters[index].name,
                std::move(arguments[index]));
        }
        return evaluate_mangling_expression(
            helper->expression, nested);
    }
    case ManglingExpressionKind::Concat: {
        ManglingValue result;
        for (const auto& operand : expression.operands) {
            result.text +=
                evaluate_mangling_expression(operand, context).text;
        }
        return result;
    }
    case ManglingExpressionKind::Decimal:
        return {
            .text = std::to_string(
                evaluate_mangling_expression(
                    expression.operands.front(), context)
                    .integer)};
    case ManglingExpressionKind::Hex: {
        char buffer[17]{};
        const auto integer =
            evaluate_mangling_expression(
                expression.operands.front(), context)
                .integer;
        const auto converted =
            std::to_chars(std::begin(buffer), std::end(buffer), integer, 16);
        return {.text = std::string(buffer, converted.ptr)};
    }
    case ManglingExpressionKind::Radix:
        return {.text = render_radix(
                    evaluate_mangling_expression(
                        expression.operands[0], context)
                        .integer,
                    evaluate_mangling_expression(
                        expression.operands[1], context)
                        .integer)};
    case ManglingExpressionKind::Bytes:
    case ManglingExpressionKind::Length:
        return {
            .type = ManglingRuntimeType::Integer,
            .text = {},
            .integer = static_cast<std::uint64_t>(
                evaluate_mangling_expression(
                    expression.operands.front(), context)
                    .text.size())};
    case ManglingExpressionKind::Path: {
        ManglingValue result;
        const auto separator =
            evaluate_mangling_expression(expression.operands[0], context).text;
        std::vector<std::string_view> components;
        const auto name = context.descriptor.qualified_name;
        for (std::size_t begin = 0; begin < name.size();) {
            const auto end = name.find("::", begin);
            components.push_back(name.substr(
                begin, end == std::string_view::npos
                           ? name.size() - begin
                           : end - begin));
            if (end == std::string_view::npos) break;
            begin = end + 2;
        }
        if (expression.operands.size() == 3 &&
            evaluate_mangling_expression(
                expression.operands[2], context)
                .boolean) {
            std::reverse(components.begin(), components.end());
        }
        for (std::size_t index = 0; index < components.size(); ++index) {
            if (index != 0) result.text += separator;
            auto item = context;
            item.text = components[index];
            item.index = static_cast<std::uint64_t>(index);
            item.count = static_cast<std::uint64_t>(components.size());
            result.text +=
                evaluate_mangling_expression(expression.operands[1], item)
                    .text;
        }
        return result;
    }
    case ManglingExpressionKind::Arguments: {
        ManglingValue result;
        const auto separator =
            evaluate_mangling_expression(expression.operands[0], context).text;
        for (std::size_t index = 0; index < context.arguments.size();
             ++index) {
            if (index != 0) result.text += separator;
            const auto& argument = context.arguments[index];
            auto item = context;
            item.text = argument.spelling;
            item.index = static_cast<std::uint64_t>(index);
            item.count =
                static_cast<std::uint64_t>(context.arguments.size());
            const auto operand =
                argument.kind == ManglingArgument::Kind::Type ? 1U : 2U;
            result.text +=
                evaluate_mangling_expression(
                    expression.operands[operand], item)
                    .text;
        }
        return result;
    }
    case ManglingExpressionKind::Parameters: {
        ManglingValue result;
        const auto separator =
            evaluate_mangling_expression(expression.operands[0], context).text;
        for (std::size_t index = 0;
             index < context.descriptor.parameters.size(); ++index) {
            if (index != 0) result.text += separator;
            const auto& parameter = context.descriptor.parameters[index];
            auto item = context;
            item.text = parameter.spelling;
            item.mode = parameter.mode;
            item.index = static_cast<std::uint64_t>(index);
            item.count = static_cast<std::uint64_t>(
                context.descriptor.parameters.size());
            result.text +=
                evaluate_mangling_expression(expression.operands[1], item)
                    .text;
        }
        return result;
    }
    case ManglingExpressionKind::Select:
        return evaluate_mangling_expression(
            expression.operands[
                evaluate_mangling_expression(
                    expression.operands[0], context)
                        .boolean
                    ? 1U
                    : 2U],
            context);
    case ManglingExpressionKind::Equal: {
        const auto left =
            evaluate_mangling_expression(expression.operands[0], context);
        const auto right =
            evaluate_mangling_expression(expression.operands[1], context);
        bool equal{};
        switch (left.type) {
        case ManglingRuntimeType::Integer:
            equal = left.integer == right.integer;
            break;
        case ManglingRuntimeType::Boolean:
            equal = left.boolean == right.boolean;
            break;
        case ManglingRuntimeType::Text:
            equal = left.text == right.text;
            break;
        }
        return {.type = ManglingRuntimeType::Boolean,
                .text = {},
                .integer = 0,
                .boolean = equal};
    }
    case ManglingExpressionKind::Not:
        return {
            .type = ManglingRuntimeType::Boolean,
            .text = {},
            .integer = 0,
            .boolean = !evaluate_mangling_expression(
                            expression.operands[0], context)
                            .boolean};
    case ManglingExpressionKind::And: {
        for (const auto& operand : expression.operands) {
            if (!evaluate_mangling_expression(operand, context).boolean) {
                return {.type = ManglingRuntimeType::Boolean,
                        .text = {},
                        .integer = 0,
                        .boolean = false};
            }
        }
        return {.type = ManglingRuntimeType::Boolean,
                .text = {},
                .integer = 0,
                .boolean = true};
    }
    case ManglingExpressionKind::Or: {
        for (const auto& operand : expression.operands) {
            if (evaluate_mangling_expression(operand, context).boolean) {
                return {.type = ManglingRuntimeType::Boolean,
                        .text = {},
                        .integer = 0,
                        .boolean = true};
            }
        }
        return {.type = ManglingRuntimeType::Boolean,
                .text = {},
                .integer = 0,
                .boolean = false};
    }
    case ManglingExpressionKind::Add: {
        const auto left =
            evaluate_mangling_expression(expression.operands[0], context)
                .integer;
        const auto right =
            evaluate_mangling_expression(expression.operands[1], context)
                .integer;
        const auto maximum = std::numeric_limits<std::uint64_t>::max();
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = right > maximum - left ? maximum : left + right};
    }
    case ManglingExpressionKind::Subtract: {
        const auto left =
            evaluate_mangling_expression(expression.operands[0], context)
                .integer;
        const auto right =
            evaluate_mangling_expression(expression.operands[1], context)
                .integer;
        return {.type = ManglingRuntimeType::Integer,
                .text = {},
                .integer = left < right ? 0 : left - right};
    }
    case ManglingExpressionKind::Slice: {
        const auto text =
            evaluate_mangling_expression(expression.operands[0], context).text;
        const auto begin =
            evaluate_mangling_expression(expression.operands[1], context)
                .integer;
        const auto count =
            evaluate_mangling_expression(expression.operands[2], context)
                .integer;
        if (begin >= text.size()) return {};
        const auto available =
            static_cast<std::uint64_t>(text.size()) - begin;
        return {.text = text.substr(
                    static_cast<std::size_t>(begin),
                    static_cast<std::size_t>(std::min(count, available)))};
    }
    case ManglingExpressionKind::Replace: {
        auto text =
            evaluate_mangling_expression(expression.operands[0], context).text;
        const auto sought =
            evaluate_mangling_expression(expression.operands[1], context).text;
        const auto replacement =
            evaluate_mangling_expression(expression.operands[2], context).text;
        if (sought.empty()) return {.text = std::move(text)};
        for (std::size_t offset = 0;
             (offset = text.find(sought, offset)) != std::string::npos;) {
            text.replace(offset, sought.size(), replacement);
            offset += replacement.size();
        }
        return {.text = std::move(text)};
    }
    case ManglingExpressionKind::Lookup: {
        const auto key =
            evaluate_mangling_expression(expression.operands[0], context)
                .text;
        for (std::size_t index = 2; index < expression.operands.size();
             index += 2) {
            if (evaluate_mangling_expression(
                    expression.operands[index], context)
                    .text == key) {
                return evaluate_mangling_expression(
                    expression.operands[index + 1], context);
            }
        }
        return evaluate_mangling_expression(
            expression.operands[1], context);
    }
    case ManglingExpressionKind::Lower:
    case ManglingExpressionKind::Upper:
        return {.text = ascii_case(
                    evaluate_mangling_expression(
                        expression.operands[0], context)
                        .text,
                    expression.kind == ManglingExpressionKind::Upper)};
    case ManglingExpressionKind::StartsWith:
    case ManglingExpressionKind::EndsWith:
    case ManglingExpressionKind::Contains: {
        const auto text =
            evaluate_mangling_expression(expression.operands[0], context).text;
        const auto fragment =
            evaluate_mangling_expression(expression.operands[1], context).text;
        const bool result =
            expression.kind == ManglingExpressionKind::StartsWith
                ? text.starts_with(fragment)
                : expression.kind == ManglingExpressionKind::EndsWith
                      ? text.ends_with(fragment)
                      : text.find(fragment) != std::string::npos;
        return {.type = ManglingRuntimeType::Boolean,
                .text = {},
                .integer = 0,
                .boolean = result};
    }
    case ManglingExpressionKind::Substitute: {
        if (!context.state) return {};
        const auto key =
            evaluate_mangling_expression(
                expression.operands[0], context)
                .text;
        const auto found = context.state->substitutions.find(key);
        if (found != context.state->substitutions.end()) {
            auto reference = context;
            reference.substitution_index = found->second;
            return evaluate_mangling_expression(
                expression.operands[2], reference);
        }
        const auto index = context.state->next_substitution++;
        context.state->substitutions.emplace(key, index);
        return evaluate_mangling_expression(
            expression.operands[1], context);
    }
    }
    return {};
}

std::string evaluate_mangling_rule(
    const ManglingEntry& mangling,
    const ManglingExpression& expression,
    const ManglingEntityDescriptor& descriptor,
    ManglingEvaluationState& state,
    std::string_view entity = {},
    std::span<const ManglingArgument> arguments = {}) {
    ManglingEvaluationContext context;
    context.descriptor = descriptor;
    context.mangling = &mangling;
    context.state = &state;
    context.entity = entity;
    context.arguments = arguments;
    context.count =
        static_cast<std::uint64_t>(arguments.size());
    auto result =
        evaluate_mangling_expression(expression, context).text;
    return state.failed ? std::string{} : result;
}

} // namespace

std::string encode_model_link_name(std::string_view qualified_name, bool label,
                                   std::string_view mangling_name) {
    return encode_model_link_name(
        {.qualified_name = qualified_name,
         .kind = label ? std::string_view{"label"}
                       : std::string_view{"entity"},
         .result = {},
         .parameters = {},
         .variadic = false},
        label, mangling_name);
}

std::string encode_model_link_name(
    const ManglingEntityDescriptor& entity, bool label,
    std::string_view mangling_name) {
    const auto* mangling = model_registry().find_mangling(mangling_name);
    if (!mangling) return {};
    ManglingEvaluationState state;
    return evaluate_mangling_rule(
        *mangling, label ? mangling->label : mangling->entity,
        entity, state);
}

std::string encode_model_generic_link_name(
    std::string_view qualified_name,
    std::span<const ManglingArgument> arguments,
    std::string_view mangling_name) {
    return encode_model_generic_link_name(
        {.qualified_name = qualified_name,
         .kind = "function",
         .result = {},
         .parameters = {},
         .variadic = false},
        arguments, mangling_name);
}

std::string encode_model_generic_link_name(
    const ManglingEntityDescriptor& descriptor,
    std::span<const ManglingArgument> arguments,
    std::string_view mangling_name) {
    const auto* mangling = model_registry().find_mangling(mangling_name);
    if (!mangling) return {};
    ManglingEvaluationState entity_state;
    const auto entity =
        evaluate_mangling_rule(
            *mangling, mangling->entity, descriptor, entity_state);
    ManglingEvaluationState generic_state;
    return evaluate_mangling_rule(
        *mangling, mangling->generic, descriptor, generic_state, entity,
        arguments);
}

} // namespace cross
