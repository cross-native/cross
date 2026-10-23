// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/model.hpp"
#include "model/model_syntax.hpp"

#include "common/builtin_type.hpp"
#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "target/target.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cctype>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
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

// ASCII classes; model files are ASCII outside strings and comments.
constexpr bool model_digit(char ch) { return ch >= '0' && ch <= '9'; }
constexpr bool model_letter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
constexpr bool model_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' ||
           ch == '\v' || ch == '\f';
}

class ModelLexer {
public:
    explicit ModelLexer(std::string_view input, unsigned line = 1)
        : input_(input), line_(line) {}

    [[nodiscard]] std::size_t offset() const { return offset_; }
    [[nodiscard]] std::size_t token_offset() const { return token_offset_; }

    // Skip the rest of a block without allocating tokens for its contents.
    void skip_block() {
        unsigned depth = 1;
        while (offset_ < input_.size() && depth != 0) {
            skip_trivia();
            if (offset_ == input_.size()) break;
            const char ch = input_[offset_++];
            if (ch == '{') ++depth;
            else if (ch == '}') --depth;
            else if (ch == '"') {
                while (offset_ < input_.size()) {
                    const char item = input_[offset_++];
                    if (item == '\n') ++line_;
                    if (item == '"') break;
                    if (item == '\\' && offset_ < input_.size()) {
                        if (input_[offset_++] == '\n') ++line_;
                    }
                }
            }
        }
    }

    ModelToken next() {
        skip_trivia();
        token_offset_ = offset_;
        const unsigned line = line_;
        if (offset_ == input_.size()) return {ModelTokenKind::End, {}, line};
        const char ch = input_[offset_];
        switch (ch) {
        // Punctuation carries no text.
        case '{': ++offset_; return {ModelTokenKind::LeftBrace, {}, line};
        case '}': ++offset_; return {ModelTokenKind::RightBrace, {}, line};
        case '[': ++offset_; return {ModelTokenKind::LeftBracket, {}, line};
        case ']': ++offset_; return {ModelTokenKind::RightBracket, {}, line};
        case '(': ++offset_; return {ModelTokenKind::LeftParen, {}, line};
        case ')': ++offset_; return {ModelTokenKind::RightParen, {}, line};
        case '=': ++offset_; return {ModelTokenKind::Equal, {}, line};
        case ',': ++offset_; return {ModelTokenKind::Comma, {}, line};
        case ';': ++offset_; return {ModelTokenKind::Semicolon, {}, line};
        case '"': return string();
        default: break;
        }
        const bool negative = ch == '-' && offset_ + 1 < input_.size() &&
                              model_digit(input_[offset_ + 1]);
        if (negative || model_digit(ch)) {
            const auto begin = offset_++;
            while (offset_ < input_.size() && model_digit(input_[offset_])) {
                ++offset_;
            }
            return {ModelTokenKind::Integer,
                    std::string(input_.substr(begin, offset_ - begin)), line};
        }
        if (model_letter(ch) || ch == '_') {
            const auto begin = offset_++;
            while (offset_ < input_.size()) {
                const char item = input_[offset_];
                if (!model_letter(item) && !model_digit(item) && item != '_' &&
                    item != '-' && item != '.') {
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
            while (offset_ < input_.size() && model_space(input_[offset_])) {
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
        const auto begin = offset_;
        auto end = begin;
        while (end < input_.size() && input_[end] != '"' &&
               input_[end] != '\\' && input_[end] != '\n') ++end;
        if (end < input_.size() && input_[end] == '"') {
            offset_ = end + 1;
            return {ModelTokenKind::String,
                    std::string(input_.substr(begin, end - begin)), line};
        }
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
    std::size_t token_offset_{};
    unsigned line_{1};
};

struct IndexedModel {
    std::string text;
    std::string origin;
    std::vector<ModelEntrySource> entries;
};

// Read declaration boundaries and architecture/name metadata. Full parsing
// diagnoses malformed headers and properties in the selected declarations.
std::vector<ModelEntrySource> index_model(std::string_view text) {
    ModelLexer lexer(text);
    std::vector<ModelEntrySource> entries;
    for (;;) {
        const auto domain = lexer.next();
        if (domain.kind == ModelTokenKind::End) break;
        ModelEntrySource entry;
        entry.offset = lexer.token_offset();
        entry.line = domain.line;
        entry.domain = domain.text;
        const auto name = lexer.next();
        entry.name = name.text;
        if (domain.kind != ModelTokenKind::Identifier ||
            name.kind != ModelTokenKind::String ||
            lexer.next().kind != ModelTokenKind::LeftBrace) {
            entry.size = text.size() - entry.offset;
            entries.push_back(std::move(entry));
            break;
        }
        const bool abi = entry.domain == "abi";
        if (abi || entry.domain == "cpu" || entry.domain == "instruction") {
            for (;;) {
                const auto token = lexer.next();
                if (token.kind == ModelTokenKind::End ||
                    token.kind == ModelTokenKind::RightBrace) break;
                if (token.kind == ModelTokenKind::LeftBrace) {
                    lexer.skip_block();
                    continue;
                }
                if (token.kind != ModelTokenKind::Identifier ||
                    (token.text != "architecture" && token.text != "aliases" &&
                     token.text != "function_selectable")) continue;
                if (lexer.next().kind != ModelTokenKind::Equal) continue;
                auto value = lexer.next();
                if (token.text == "architecture" &&
                    value.kind == ModelTokenKind::String) {
                    entry.architecture = value.text;
                    if (!abi) {
                        lexer.skip_block();
                        break;
                    }
                } else if (abi && token.text == "aliases") {
                    if (value.kind == ModelTokenKind::String) {
                        entry.aliases.push_back(value.text);
                    } else if (value.kind == ModelTokenKind::LeftBracket) {
                        while ((value = lexer.next()).kind == ModelTokenKind::String) {
                            entry.aliases.push_back(value.text);
                            if (lexer.next().kind != ModelTokenKind::Comma) break;
                        }
                    }
                } else if (abi && token.text == "function_selectable") {
                    entry.function_selectable = value.text == "true";
                }
            }
        } else {
            lexer.skip_block();
        }
        entry.size = lexer.offset() - entry.offset;
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool indexed_name(const ModelEntrySource& entry, std::string_view name) {
    return entry.name == name ||
           std::find(entry.aliases.begin(), entry.aliases.end(), name) != entry.aliases.end();
}

bool indexed_abi(std::span<const IndexedModel> sources, std::string_view architecture,
                 std::string_view name, bool function_selectable = false) {
    for (const auto& source : sources) {
        for (const auto& entry : source.entries) {
            if (entry.domain == "abi" &&
                (architecture.empty() || entry.architecture == architecture) &&
                (!function_selectable || entry.function_selectable) && indexed_name(entry, name)) {
                return true;
            }
        }
    }
    return false;
}

// References to CPUs of other architectures only need their indexed names.
// Their feature modifiers are checked when that architecture is loaded.
bool indexed_cpu(std::span<const IndexedModel> sources, std::string_view architecture,
                 std::string_view name, bool modifiers) {
    for (const auto& source : sources) {
        for (const auto& entry : source.entries) {
            if (entry.domain != "cpu" || entry.architecture != architecture) continue;
            if (entry.name == name ||
                (modifiers && name.starts_with(entry.name + '+'))) return true;
        }
    }
    return false;
}

// The address widths of the objects that the triples of `architecture`
// produce, in increasing order.
std::vector<unsigned> triple_address_widths(std::string_view architecture) {
    std::vector<unsigned> result;
    for (const auto* target : all_targets()) {
        if (target->architecture != architecture) continue;
        for (const auto& prefix : target->triple_prefixes) {
            if (std::find(result.begin(), result.end(), prefix.address_bits) ==
                result.end()) {
                result.push_back(prefix.address_bits);
            }
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

// The size and alignment in bytes of a variadic-state type: a builtin
// scalar type, or a builtin type followed by one or more `*`.
std::optional<std::pair<unsigned, unsigned>> variadic_state_layout(
    std::string_view type, unsigned address_bits,
    const TargetDataLayout& layout) {
    bool pointer{};
    while (type.ends_with('*')) {
        pointer = true;
        type.remove_suffix(1);
    }
    const auto kind = builtin_kind(type);
    if (!kind) return std::nullopt;
    const auto size = pointer ? std::optional<std::uint64_t>(
                                    (address_bits + 7U) / 8U)
                              : builtin_storage_size(*kind, address_bits,
                                                     layout.f80_storage_bytes);
    if (!size) return std::nullopt;
    const auto alignment = natural_storage_alignment(
        *size, !pointer && *kind == BuiltinType::F80,
        layout.natural_alignment_limit, layout.f80_alignment);
    return std::pair{static_cast<unsigned>(*size),
                     static_cast<unsigned>(alignment)};
}

// Why a cpu's or an ABI's features are invalid, or nothing: each must be a
// target feature, listed once, and none may conflict with another or with
// what another enables.
std::optional<std::string> feature_problem(
    const TargetInfo& target, const std::vector<std::string>& features,
    std::string_view entry = "cpu") {
    const auto& table = target.subtargets->features;
    // The index of a feature, or the table size.
    const auto find = [&](std::string_view name) {
        return static_cast<std::size_t>(
            std::find_if(table.begin(), table.end(),
                         [&](const SubtargetFeature& feature) {
                             return feature.name == name;
                         }) -
            table.begin());
    };
    std::vector<bool> enabled(table.size());
    std::vector<std::string_view> pending;
    for (const auto& name : features) {
        if (find(name) == table.size()) {
            return "unknown " + std::string(target.architecture) +
                   " feature '" + name + "'";
        }
        if (std::count(features.begin(), features.end(), name) > 1) {
            return std::string(entry) + " features list '" + name + "' twice";
        }
        pending.push_back(name);
    }
    while (!pending.empty()) {
        const auto index = find(pending.back());
        pending.pop_back();
        if (index == table.size() || enabled[index]) continue;
        enabled[index] = true;
        pending.insert(pending.end(), table[index].prerequisites.begin(),
                       table[index].prerequisites.end());
        pending.insert(pending.end(), table[index].implies.begin(),
                       table[index].implies.end());
    }
    for (std::size_t index = 0; index < table.size(); ++index) {
        if (!enabled[index]) continue;
        for (const auto conflict : table[index].conflicts) {
            const auto other = find(conflict);
            if (other != table.size() && enabled[other]) {
                return std::string(entry) + " features '" +
                       std::string(table[index].name) + "' and '" +
                       std::string(conflict) + "' conflict";
            }
        }
    }
    return std::nullopt;
}

// Whether `name` is a target extension that `cc --print-features` lists.
bool target_extension(const TargetInfo& target, std::string_view name) {
    const auto& features = target.subtargets->features;
    return std::any_of(features.begin(), features.end(),
                       [&](const SubtargetFeature& feature) {
                           return feature.selectable && feature.name == name;
                       });
}

struct ModelDocument {
    std::vector<AbiEntry> abis;
    std::vector<ManglingEntry> manglings;
    std::vector<OptimizationEntry> optimizations;
    std::vector<ProfileEntry> profiles;
    std::vector<DebugEntry> debugs;
    std::vector<CpuEntry> cpus;
    std::vector<DataBaseEntry> data_bases;
    std::vector<InitTableEntry> init_tables;
    std::vector<ModelInstruction> instructions;
};

// Instruction costs are cycle counts that cost sums must not overflow.
constexpr unsigned max_cpu_cost = 65535;

class ModelParser {
public:
    ModelParser(std::string_view text, std::string origin,
                std::span<const ModelEntrySource> entries)
        : text_(text), entries_(entries), lexer_(text), origin_(std::move(origin)) {}

    std::optional<ModelDocument> parse() {
        ModelDocument result;
        result.instructions.reserve(static_cast<std::size_t>(std::count_if(
            entries_.begin(), entries_.end(),
            [](const ModelEntrySource& entry) { return entry.domain == "instruction"; })));
        while (error_.empty() && next_entry_ < entries_.size()) {
            const auto& source = entries_[next_entry_++];
            lexer_ = ModelLexer(text_.substr(source.offset, source.size), source.line);
            advance();
            if (current_.kind != ModelTokenKind::Identifier) {
                fail(current_.line,
                     "expected model domain: abi, mangling, optimization, "
                     "profile, debug, cpu, data_base, init_table, or "
                     "instruction");
                break;
            }
            const auto domain = current_.text;
            const auto line = current_.line;
            advance();
            if (domain != "abi" && domain != "mangling" &&
                domain != "optimization" && domain != "profile" &&
                domain != "debug" && domain != "cpu" &&
                domain != "data_base" && domain != "init_table" &&
                domain != "instruction") {
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
                    entry->source = position(line);
                    result.manglings.push_back(std::move(*entry));
                }
                continue;
            }
            if (domain == "abi") {
                if (auto entries = parse_abi(std::move(name), line)) {
                    for (auto& entry : *entries) {
                        entry.source = position(line);
                        result.abis.push_back(std::move(entry));
                    }
                }
                continue;
            }
            if (domain == "cpu") {
                if (auto entry = parse_cpu(std::move(name), line)) {
                    entry->source = position(line);
                    result.cpus.push_back(std::move(*entry));
                }
                continue;
            }
            auto properties = parse_properties();
            if (!properties || !error_.empty()) break;
            if (domain == "optimization") {
                if (auto entry =
                        make_optimization(name, *properties, line)) {
                    entry->source = position(line);
                    result.optimizations.push_back(std::move(*entry));
                }
            } else if (domain == "debug") {
                if (auto entry = make_debug(name, *properties, line)) {
                    entry->source = position(line);
                    result.debugs.push_back(std::move(*entry));
                }
            } else if (domain == "data_base") {
                if (auto entry = make_data_base(name, *properties, line)) {
                    entry->source = position(line);
                    result.data_bases.push_back(std::move(*entry));
                }
            } else if (domain == "init_table") {
                if (auto entry = make_init_table(name, *properties, line)) {
                    entry->source = position(line);
                    result.init_tables.push_back(std::move(*entry));
                }
            } else if (domain == "instruction") {
                ModelProblem problem;
                if (auto entry = make_instruction(name, line, origin_, *properties,
                                                  problem)) {
                    result.instructions.push_back(std::move(*entry));
                } else {
                    fail(problem.line, problem.message);
                }
            } else if (auto entry = make_profile(name, *properties, line)) {
                entry->source = position(line);
                result.profiles.push_back(std::move(*entry));
            }
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    [[nodiscard]] const std::string& error_position() const {
        return error_position_;
    }
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    [[nodiscard]] std::string position(unsigned line) const {
        return origin_ + ':' + std::to_string(line);
    }

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
            error_position_ = position(line);
            error_ = std::move(message);
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

    // An ABI entry's blocks in source order, parallel to the entry's banks,
    // rules, variadic states, and variadic shadows.
    struct AbiBlocks {
        std::vector<AbiNestedBlock> banks;
        std::vector<AbiNestedBlock> rules;
        std::vector<AbiNestedBlock> variadic_states;
        std::vector<AbiNestedBlock> variadic_shadows;
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
                result.text = std::move(current_.text);
            }
            advance();
            return result;
        }
        if (current_.kind == ModelTokenKind::Integer) {
            result.kind = ModelValue::Kind::Integer;
            const auto digits = current_.text.starts_with('-') ? 1U : 0U;
            const auto parsed = std::from_chars(
                current_.text.data() + digits,
                current_.text.data() + current_.text.size(),
                result.integer);
            result.negative = digits != 0 && result.integer != 0;
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
                result.list.push_back(std::move(current_.text));
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
        auto name = std::move(current_.text);
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
        if (result.contains(name)) {
            fail(line, "duplicate model property '" + name + "'");
            return false;
        }
        result.add(std::move(name), ModelProperty{std::move(*value), line});
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

    std::optional<std::vector<AbiEntry>> parse_abi(std::string name,
                                                   unsigned entry_line) {
        ModelProperties properties;
        AbiBlocks blocks;
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
            auto& destination = bank ? blocks.banks
                              : rule ? blocks.rules
                              : state ? blocks.variadic_states
                                      : blocks.variadic_shadows;
            destination.push_back(
                {std::move(block_name), std::move(*block_properties), line});
        }
        if (!expect(ModelTokenKind::RightBrace, "'}' after ABI entry")) {
            return std::nullopt;
        }
        return make_abi(std::move(name), properties, blocks, entry_line);
    }

    bool known_properties(
        const ModelProperties& properties,
        std::initializer_list<std::string_view> names,
        std::string_view prefix = {}) {
        for (const auto& [name, property] : properties) {
            if (!prefix.empty() && name.starts_with(prefix)) continue;
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                fail(property.line, "unknown model property '" + name + "'");
                return false;
            }
        }
        return true;
    }

    // A nonzero `required_at` makes the property required and is the line of
    // the entry or block that lacks it.
    std::optional<std::string> text_property(
        const ModelProperties& properties, std::string_view name,
        unsigned required_at = 0) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) {
            if (required_at != 0) {
                fail(required_at,
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

    // A text naming one entry or a list naming several, each once.
    std::vector<std::string> names_property(
        const ModelProperties& properties, std::string_view name) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) return {};
        const auto& value = found->second.value;
        if (value.kind == ModelValue::Kind::Text) return {value.text};
        if (value.kind != ModelValue::Kind::List) {
            fail(found->second.line, "model property '" + std::string(name) +
                                         "' must be text or a list");
            return {};
        }
        for (auto item = value.list.begin(); item != value.list.end(); ++item) {
            if (std::find(value.list.begin(), item, *item) != item) {
                fail(found->second.line, "model property '" +
                                             std::string(name) + "' lists '" +
                                             *item + "' twice");
                return {};
            }
        }
        return value.list;
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
            found->second.value.negative ||
            found->second.value.integer > static_cast<std::uint64_t>(
                                                std::numeric_limits<unsigned>::max())) {
            fail(found->second.line,
                 "model property '" + std::string(name) +
                     "' must be an unsigned integer");
            return std::nullopt;
        }
        return static_cast<unsigned>(found->second.value.integer);
    }

    // The few properties that state offsets may be negative.
    std::optional<int> signed_property(
        const ModelProperties& properties, std::string_view name) {
        const auto found = properties.find(std::string(name));
        if (found == properties.end()) return std::nullopt;
        const auto& value = found->second.value;
        const auto limit = static_cast<std::uint64_t>(
                               std::numeric_limits<int>::max()) +
                           (value.negative ? 1U : 0U);
        if (value.kind != ModelValue::Kind::Integer || value.integer > limit) {
            fail(found->second.line, "model property '" + std::string(name) +
                                         "' must be an integer");
            return std::nullopt;
        }
        return value.negative
                   ? static_cast<int>(-static_cast<std::int64_t>(value.integer))
                   : static_cast<int>(value.integer);
    }

    std::optional<AbiRegisterFailure> abi_failure(
        std::string_view spelling, unsigned line) {
        if (spelling == "stack") return AbiRegisterFailure::Stack;
        if (spelling == "partial") return AbiRegisterFailure::Partial;
        if (spelling == "error") return AbiRegisterFailure::Error;
        if (spelling == "exhaust") return AbiRegisterFailure::Exhaust;
        fail(line, "ABI register failure must be 'stack', 'partial', "
                   "'exhaust', or 'error'");
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
            {"structure", AbiValueKind::Structure},
            {"union", AbiValueKind::Union},
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

    std::optional<AbiExtensionKind> abi_extension(
        std::string_view spelling, unsigned line) {
        static constexpr std::pair<std::string_view, AbiExtensionKind>
            extensions[] = {
                {"none", AbiExtensionKind::None},
                {"zero", AbiExtensionKind::Zero},
                {"sign", AbiExtensionKind::Sign},
            };
        const auto found = std::find_if(
            std::begin(extensions), std::end(extensions),
            [&](const auto& item) { return item.first == spelling; });
        if (found != std::end(extensions)) return found->second;
        fail(line, "unknown ABI register extension '" +
                       std::string(spelling) + "'");
        return std::nullopt;
    }

    // One entry, or without address_bits one per address width of the
    // architecture's triples, each checked at its width.
    std::optional<std::vector<AbiEntry>> make_abi(
        std::string name, const ModelProperties& properties,
        const AbiBlocks& blocks, unsigned line) {
        if (!known_properties(
                properties,
                {"architecture", "address_bits", "aliases",
                 "llvm_calling_convention", "gcc_calling_attribute",
                 "compilation_selectable", "function_selectable",
                 "elf_abi_tag", "features", "private_carrier_bits",
                 "natural_alignment_limit",
                 "call_clobbers", "pic_call_clobbers",
                 "call_preserved_views", "argument_register_failure",
                 "result_register_failure", "stack_layout",
                 "argument_stack_base", "stack_alignment",
                 "stack_slot_bytes", "return_address_bytes",
                 "stack_order", "variadic_supported",
                 "variadic_count_cursor", "variadic_count_register",
                 "variadic_count_bits", "variadic_save_banks",
                 "variadic_save_alignment", "variadic_home_bank",
                 "variadic_home_base", "variadic_home_stride",
                 "variadic_va_list_bytes",
                 "variadic_va_list_alignment"}, "type.")) {
            return std::nullopt;
        }
        AbiEntry result;
        // type.NAME = "T" names the scalar arithmetic type T, such as the C
        // type NAME of a C ABI.
        for (const auto& [property_name, property] : properties) {
            if (!property_name.starts_with("type.")) continue;
            const auto type_name = property_name.substr(5);
            const auto spelling = text_property(properties, property_name);
            if (!spelling) return std::nullopt;
            const auto kind = builtin_kind(*spelling);
            if (type_name.empty() || !kind || *kind == BuiltinType::Void ||
                *kind == BuiltinType::Label) {
                fail(property.line, type_name.empty()
                    ? "ABI type property needs a name after 'type.'"
                    : "ABI type '" + type_name + "' must be a scalar arithmetic type such as i32 or f64");
                return std::nullopt;
            }
            result.named_types.emplace_back(type_name, *kind);
        }
        result.canonical_name = std::move(name);
        const auto architecture =
            text_property(properties, "architecture", line);
        if (!architecture || !error_.empty()) return std::nullopt;
        const auto* target_info = architecture_target(*architecture);
        if (!target_info) {
            fail(properties.at("architecture").line,
                 "unknown architecture '" + *architecture + "'");
            return std::nullopt;
        }
        result.architecture = *architecture;
        const auto address_bits =
            unsigned_property(properties, "address_bits");
        if (!error_.empty()) return std::nullopt;
        if (address_bits && *address_bits == 0) {
            fail(line, "ABI address_bits must be nonzero");
            return std::nullopt;
        }
        if (address_bits &&
            *address_bits > std::numeric_limits<std::uint16_t>::max()) {
            fail(line, "ABI address_bits exceeds the supported range");
            return std::nullopt;
        }
        if (const auto llvm =
                text_property(properties, "llvm_calling_convention")) {
            result.llvm_calling_convention = *llvm;
        }
        if (const auto gcc =
                text_property(properties, "gcc_calling_attribute")) {
            result.gcc_calling_attribute = *gcc;
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
        if (const auto value = text_property(properties, "elf_abi_tag")) {
            if (value->empty()) {
                fail(line, "ABI elf_abi_tag must not be empty");
                return std::nullopt;
            }
            result.elf_abi_tag = *value;
        }
        if (auto features = list_property(properties, "features")) {
            auto problem = feature_problem(*target_info, *features, "ABI");
            for (const auto& feature : *features) {
                if (problem) break;
                if (!target_extension(*target_info, feature)) {
                    problem = "ABI feature '" + feature +
                              "' is a CPU fact that options cannot select";
                }
            }
            if (problem) {
                fail(properties.at("features").line, std::move(*problem));
                return std::nullopt;
            }
            result.features = std::move(*features);
        }
        if (const auto value =
                unsigned_property(properties, "private_carrier_bits")) {
            if (*value == 0) {
                fail(line, "ABI private_carrier_bits must be nonzero");
                return std::nullopt;
            }
            result.private_carrier_bits = *value;
        }
        if (const auto value =
                unsigned_property(properties, "natural_alignment_limit")) {
            if (*value == 0 || (*value & (*value - 1U)) != 0) {
                fail(line, "ABI natural_alignment_limit must be a power of two");
                return std::nullopt;
            }
            result.natural_alignment_limit = *value;
        }
        if (const auto value = list_property(properties, "call_clobbers")) {
            result.call_clobbers = *value;
        }
        if (const auto value = list_property(properties, "pic_call_clobbers")) {
            result.pic_call_clobbers = *value;
        }
        if (const auto value =
                list_property(properties, "call_preserved_views")) {
            result.call_preserved_views = *value;
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
                signed_property(properties, "variadic_home_base")) {
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
        for (const auto& block : blocks.banks) {
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
                text_property(block.properties, "class", block.line);
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
        for (const auto& block : blocks.rules) {
            if (!rule_names.insert(block.name).second) {
                fail(block.line, "duplicate ABI rule '" + block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(
                    block.properties,
                    {"match", "action", "bank", "min_bits", "max_bits",
                     "unit_bits", "carrier_bits", "extension",
                     "max_elements",
                     "cursor_alignment", "cursor_advance",
                     "argument_limit", "requires_unused_banks",
                     "stack_alignment", "stack_size",
                     "max_stack_alignment", "applies_to",
                     "register_failure", "requires_features",
                     "forbids_features", "merge_banks", "member_banks",
                     "members", "padding_bank",
                     "require_natural_alignment", "uniform_members",
                     "max_members"})) {
                return std::nullopt;
            }
            AbiRule rule;
            rule.canonical_name = block.name;
            const auto matches =
                list_property(block.properties, "match");
            const auto action =
                text_property(block.properties, "action", block.line);
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
                    text_property(block.properties, "extension")) {
                const auto parsed = abi_extension(*value, block.line);
                if (!parsed) return std::nullopt;
                rule.extension = *parsed;
            }
            if (const auto value =
                    unsigned_property(block.properties, "max_elements")) {
                rule.max_elements = *value;
            }
            // A stated alignment, or "value" for the placed value's own.
            if (const auto found = block.properties.find("cursor_alignment");
                found != block.properties.end() &&
                found->second.value.kind == ModelValue::Kind::Text) {
                if (found->second.value.text != "value") {
                    fail(found->second.line,
                         "ABI rule cursor_alignment must be a power of two "
                         "or \"value\"");
                    return std::nullopt;
                }
                rule.cursor_alignment_value = true;
            } else if (const auto value = unsigned_property(
                           block.properties, "cursor_alignment")) {
                rule.cursor_alignment = *value;
            }
            if (const auto value = unsigned_property(
                    block.properties, "cursor_advance")) {
                rule.cursor_advance = *value;
            }
            if (const auto value = unsigned_property(
                    block.properties, "argument_limit")) {
                rule.argument_limit = *value;
            }
            if (const auto value = list_property(
                    block.properties, "requires_unused_banks")) {
                rule.requires_unused_banks = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "stack_alignment")) {
                rule.stack_alignment = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "stack_size")) {
                rule.stack_size = *value;
            }
            if (const auto value = unsigned_property(
                    block.properties, "max_stack_alignment")) {
                rule.max_stack_alignment = *value;
            }
            if (const auto value =
                    list_property(block.properties, "merge_banks")) {
                rule.merge_banks = *value;
            }
            if (const auto value =
                    list_property(block.properties, "member_banks")) {
                rule.member_banks = *value;
            }
            rule.padding_bank =
                text_property(block.properties, "padding_bank").value_or("");
            if (const auto value =
                    text_property(block.properties, "members")) {
                if (*value == "only") {
                    rule.members = AbiMemberScope::Only;
                } else if (*value == "none") {
                    rule.members = AbiMemberScope::None;
                } else if (*value != "any") {
                    fail(block.line,
                         "ABI rule members must be 'any', 'only', or 'none'");
                    return std::nullopt;
                }
            }
            if (const auto value = bool_property(
                    block.properties, "require_natural_alignment")) {
                rule.require_natural_alignment = *value;
            }
            if (const auto value = bool_property(
                    block.properties, "uniform_members")) {
                rule.uniform_members = *value;
            }
            if (const auto value =
                    unsigned_property(block.properties, "max_members")) {
                rule.max_members = *value;
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
            if (rule.max_stack_alignment != 0 &&
                (rule.max_stack_alignment & (rule.max_stack_alignment - 1U)) != 0) {
                fail(block.line,
                     "ABI rule max_stack_alignment must be a power of two");
                return std::nullopt;
            }
            if (rule.cursor_alignment == 0 ||
                (rule.cursor_alignment & (rule.cursor_alignment - 1U)) != 0) {
                fail(block.line,
                     "ABI rule cursor_alignment must be a nonzero power of two");
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
        for (const auto& block : blocks.variadic_states) {
            if (!variadic_state_names.insert(block.name).second) {
                fail(block.line, "duplicate ABI variadic state '" +
                                     block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(
                    block.properties,
                    {"type", "kind", "cursor", "base", "stride", "limit",
                     "alignment", "llvm_va_list_offset", "fields"})) {
                return std::nullopt;
            }
            AbiVariadicState state;
            state.canonical_name = block.name;
            const auto type = text_property(block.properties, "type", block.line);
            const auto kind = text_property(block.properties, "kind", block.line);
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
            } else if (*kind == "record_address") {
                state.kind = AbiVariadicStateKind::RecordAddress;
            } else {
                fail(block.line,
                     "ABI variadic-state kind must be 'cursor_offset', "
                     "'stack_address', 'cursor_address', "
                     "'register_save_address', or 'record_address'");
                return std::nullopt;
            }
            const bool record = state.kind == AbiVariadicStateKind::RecordAddress;
            if (const auto fields = list_property(block.properties, "fields")) {
                state.field_names = *fields;
            }
            if (record != block.properties.contains("fields") ||
                (record && state.field_names.empty())) {
                fail(block.line,
                     "ABI variadic state fields are required by, and only "
                     "valid for, a record_address state");
                return std::nullopt;
            }
            static constexpr const char* cursor_properties[] = {
                "cursor", "base", "stride", "limit", "llvm_va_list_offset"};
            if (record &&
                (!state.type.ends_with('*') ||
                 std::any_of(std::begin(cursor_properties),
                             std::end(cursor_properties),
                             [&](const char* name) {
                                 return block.properties.contains(name);
                             }))) {
                fail(block.line,
                     "ABI variadic record_address state requires a pointer "
                     "type and takes no cursor, base, stride, limit, or "
                     "llvm_va_list_offset");
                return std::nullopt;
            }
            state.cursor =
                text_property(block.properties, "cursor").value_or("");
            if (const auto value =
                    signed_property(block.properties, "base")) {
                state.base = *value;
            }
            if (const auto value =
                    signed_property(block.properties, "stride")) {
                state.stride = *value;
            }
            state.limit = unsigned_property(block.properties, "limit");
            if (state.limit && state.cursor.empty()) {
                fail(block.line,
                     "ABI variadic state limit requires a cursor");
                return std::nullopt;
            }
            if (const auto value =
                    unsigned_property(block.properties, "alignment")) {
                state.alignment = *value;
            }
            state.llvm_va_list_offset = unsigned_property(
                block.properties, "llvm_va_list_offset");
            // Without a cursor, n is 0 and the state is its base.
            if (state.cursor.empty() != (state.stride == 0) &&
                state.kind != AbiVariadicStateKind::StackAddress) {
                fail(block.line,
                     "ABI variadic state with a cursor requires a nonzero "
                     "stride, and one without a cursor no stride");
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
        for (const auto& block : blocks.variadic_shadows) {
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
                text_property(block.properties, "source_bank", block.line);
            const auto target =
                text_property(block.properties, "target_bank", block.line);
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
        if (result.variadic_home_base < 0 &&
            result.variadic_home_bank.empty()) {
            fail(line, "a negative variadic_home_base requires a home bank");
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
        std::vector<AbiEntry> entries;
        if (address_bits) {
            result.address_bits = *address_bits;
            entries.push_back(std::move(result));
        } else {
            for (const auto bits : triple_address_widths(result.architecture)) {
                auto& entry = entries.emplace_back(result);
                entry.address_bits = bits;
                entry.address_bits_from_triple = true;
            }
        }
        for (auto& entry : entries) {
            if (!variadic_layout(*target_info, entry, properties, blocks) ||
                !check_abi(*target_info, entry, properties, blocks, line)) {
                return std::nullopt;
            }
            // Keep the partial-preservation spelling equivalent to omitting
            // that storage from the volatile set on every target.
            std::erase_if(entry.call_clobbers, [&](const std::string& name) {
                const auto* reg = find_register(*target_info, name);
                return reg && std::ranges::any_of(
                    entry.call_preserved_views, [&](const std::string& kept) {
                        return find_register(*target_info, kept)->storage ==
                               reg->storage;
                    });
            });
        }
        return entries;
    }

    // Checks the type of each variadic state and, without
    // variadic_va_list_bytes, sizes the va_list area as a record of the
    // states: one with llvm_va_list_offset at that offset, any other after
    // the state before it.
    bool variadic_layout(const TargetInfo& target, AbiEntry& abi,
                         const ModelProperties& properties,
                         const AbiBlocks& blocks) {
        unsigned end{};
        unsigned next{};
        unsigned alignment = abi.variadic_va_list_alignment;
        for (std::size_t index = 0; index < abi.variadic_states.size();
             ++index) {
            const auto& state = abi.variadic_states[index];
            auto data_layout = target.data_layout;
            if (abi.natural_alignment_limit != 0) {
                data_layout.natural_alignment_limit =
                    abi.natural_alignment_limit;
            }
            const auto layout = variadic_state_layout(
                state.type, abi.address_bits, data_layout);
            if (!layout) {
                fail(blocks.variadic_states[index].line,
                     "ABI variadic-state type '" + state.type +
                         "' is not a scalar or pointer type");
                return false;
            }
            const auto [size, field_alignment] = *layout;
            const auto offset =
                state.llvm_va_list_offset
                    ? *state.llvm_va_list_offset
                    : (next + field_alignment - 1U) / field_alignment *
                          field_alignment;
            next = offset + size;
            end = std::max(end, next);
            alignment = std::max(alignment, field_alignment);
        }
        if (!properties.contains("variadic_va_list_bytes") &&
            !abi.variadic_states.empty()) {
            abi.variadic_va_list_bytes =
                (end + alignment - 1U) / alignment * alignment;
        }
        // A record_address state's record holds the other states it names,
        // each at the next offset its type's alignment allows.
        for (std::size_t index = 0; index < abi.variadic_states.size();
             ++index) {
            auto& state = abi.variadic_states[index];
            if (state.kind != AbiVariadicStateKind::RecordAddress) continue;
            const auto line = blocks.variadic_states[index].line;
            auto data_layout = target.data_layout;
            if (abi.natural_alignment_limit != 0) {
                data_layout.natural_alignment_limit =
                    abi.natural_alignment_limit;
            }
            unsigned offset{};
            state.record_alignment = state.alignment;
            for (const auto& name : state.field_names) {
                const auto field = std::find_if(
                    abi.variadic_states.begin(), abi.variadic_states.end(),
                    [&](const AbiVariadicState& candidate) {
                        return candidate.canonical_name == name;
                    });
                if (field == abi.variadic_states.end() ||
                    field->kind == AbiVariadicStateKind::RecordAddress ||
                    std::count(state.field_names.begin(),
                               state.field_names.end(), name) > 1) {
                    fail(line, "ABI variadic state '" + state.canonical_name +
                                   "' fields must name other states once, "
                                   "none of kind record_address");
                    return false;
                }
                const auto [size, field_alignment] = *variadic_state_layout(
                    field->type, abi.address_bits, data_layout);
                offset = (offset + field_alignment - 1U) / field_alignment *
                         field_alignment;
                state.fields.push_back(
                    {AbiStateId{static_cast<std::uint16_t>(
                         field - abi.variadic_states.begin())},
                     offset});
                offset += size;
                state.record_alignment =
                    std::max(state.record_alignment, field_alignment);
            }
            state.record_bytes = (offset + state.record_alignment - 1U) /
                                 state.record_alignment *
                                 state.record_alignment;
        }
        return true;
    }

    // Checks an ABI entry against its architecture and the references among
    // its banks, rules, cursors, and stack regions.
    bool check_abi(const TargetInfo& target, const AbiEntry& abi,
                   const ModelProperties& properties, const AbiBlocks& blocks,
                   unsigned line) {
        const auto model =
            "ABI model '" + abi.canonical_name + "'" +
            (abi.address_bits_from_triple
                 ? " with " + std::to_string(abi.address_bits) +
                       "-bit addresses"
                 : std::string{});
        const auto property_line = [](const ModelProperties& owner,
                                      std::string_view name) {
            return owner.at(std::string(name)).line;
        };
        if (!abi.elf_abi_tag.empty()) {
            const auto tag = std::find_if(
                target.elf_abi_tags.begin(), target.elf_abi_tags.end(),
                [&](const ElfAbiTagEntry& entry) {
                    return entry.name == abi.elf_abi_tag;
                });
            const auto tag_line = property_line(properties, "elf_abi_tag");
            if (tag == target.elf_abi_tags.end()) {
                fail(tag_line, model + " requests ELF ABI tag '" +
                                   abi.elf_abi_tag + "', which " +
                                   std::string(target.architecture) +
                                   " does not define");
                return false;
            }
            if (tag->address_bits != 0 &&
                tag->address_bits != abi.address_bits) {
                fail(tag_line, model + " requests ELF ABI tag '" +
                                   abi.elf_abi_tag + "', which requires " +
                                   std::to_string(tag->address_bits) +
                                   "-bit addresses");
                return false;
            }
        }
        for (std::size_t index = 0; index < abi.banks.size(); ++index) {
            const auto& bank = abi.banks[index];
            const auto& block = blocks.banks[index];
            const auto registers_valid =
                [&](const std::vector<std::string>& names,
                    std::string_view property) {
                    for (const auto& name : names) {
                        const auto* entry = find_register(target, name);
                        if (entry &&
                            entry->register_class == bank.register_class &&
                            entry->bits >= bank.register_bits) {
                            continue;
                        }
                        fail(property_line(block.properties, property),
                             model + " bank '" + bank.canonical_name +
                                 "' property '" + std::string(property) +
                                 "' contains an incompatible register '" +
                                 name + "'");
                        return false;
                    }
                    return true;
                };
            if (!registers_valid(bank.arguments, "arguments") ||
                !registers_valid(bank.results, "results")) {
                return false;
            }
        }
        for (const auto* list : {&abi.call_clobbers, &abi.pic_call_clobbers}) {
            const auto* property =
                list == &abi.call_clobbers ? "call_clobbers" : "pic_call_clobbers";
            for (const auto& clobber : *list) {
                if (clobber != "memory" && clobber != "flags" &&
                    !find_register(target, clobber)) {
                    fail(property_line(properties, property),
                         model + " contains unknown call clobber '" + clobber +
                             "'");
                    return false;
                }
            }
        }
        // A preserved view is part of a register that a call keeps, once.
        std::unordered_set<std::string_view> preserved_storage;
        for (const auto& name : abi.call_preserved_views) {
            const auto* view = find_register(target, name);
            if (!view ||
                !preserved_storage.insert(view->storage).second) {
                fail(property_line(properties, "call_preserved_views"),
                     model + " call_preserved_views names '" + name +
                         "', which is not a register view, or a second view "
                         "of one register");
                return false;
            }
        }
        const auto has_stack_region = [&](AbiStackRegion sought) {
            return std::find(abi.stack_order.begin(), abi.stack_order.end(),
                             sought) != abi.stack_order.end();
        };
        std::unordered_set<std::string_view> slot_cursors;
        bool needs_argument_stack{};
        bool needs_result_stack{};
        for (std::size_t index = 0; index < abi.rules.size(); ++index) {
            const auto& rule = abi.rules[index];
            const auto rule_line = blocks.rules[index].line;
            const auto rule_model =
                model + " rule '" + rule.canonical_name + "'";
            for (const auto* features :
                 {&rule.required_features, &rule.forbidden_features}) {
                for (const auto& feature : *features) {
                    if (target_extension(target, feature)) continue;
                    fail(rule_line, rule_model + " names unknown " +
                                        std::string(target.architecture) +
                                        " feature '" + feature + "'");
                    return false;
                }
            }
            std::unordered_set<std::string_view> required_features;
            for (const auto& feature : rule.required_features) {
                if (!required_features.insert(feature).second) {
                    fail(rule_line,
                         rule_model + " contains a duplicate required feature");
                    return false;
                }
            }
            std::unordered_set<std::string_view> forbidden_features;
            for (const auto& feature : rule.forbidden_features) {
                if (!forbidden_features.insert(feature).second ||
                    required_features.contains(feature)) {
                    fail(rule_line,
                         rule_model + " contains a duplicate or "
                                      "contradictory forbidden feature");
                    return false;
                }
            }
            const bool needs_bank =
                rule.action == AbiRuleAction::Direct ||
                rule.action == AbiRuleAction::Split ||
                rule.action == AbiRuleAction::Coerce ||
                rule.action == AbiRuleAction::Indirect;
            if (rule.extension != AbiExtensionKind::None &&
                rule.action != AbiRuleAction::Direct &&
                rule.action != AbiRuleAction::Split &&
                rule.action != AbiRuleAction::Coerce) {
                fail(rule_line,
                     rule_model +
                         " uses extension without a direct register transport");
                return false;
            }
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
                fail(rule_line,
                     rule_model + " names unknown bank '" + rule.bank + "'");
                return false;
            }
            if (rule.extension != AbiExtensionKind::None &&
                bank != abi.banks.end() &&
                bank->register_class != "integer") {
                fail(rule_line,
                     rule_model +
                         " uses extension with a non-integer register bank");
                return false;
            }
            if (!needs_bank && !rule.bank.empty()) {
                fail(rule_line, rule_model + " supplies a bank to an action "
                                             "that does not use one");
                return false;
            }
            if (needs_bank) {
                if (rule.arguments && bank->arguments.empty()) {
                    fail(rule_line,
                         rule_model + " applies to arguments but its bank "
                                      "has no argument registers");
                    return false;
                }
                const auto& result_registers =
                    rule.action == AbiRuleAction::Indirect ? bank->arguments
                                                           : bank->results;
                if (rule.results && result_registers.empty()) {
                    fail(rule_line,
                         rule_model + " applies to results but its bank has "
                                      "no compatible result channel");
                    return false;
                }
                if (abi.stack_layout == AbiStackLayout::Slots &&
                    rule.arguments) {
                    slot_cursors.insert(bank->cursor);
                }
            }
            if (rule.max_bits != 0 && rule.min_bits > rule.max_bits) {
                fail(rule_line,
                     rule_model + " has min_bits greater than max_bits");
                return false;
            }
            if ((rule.action == AbiRuleAction::Split ||
                 rule.action == AbiRuleAction::Coerce) &&
                rule.unit_bits == 0 &&
                (bank == abi.banks.end() || bank->register_bits == 0)) {
                fail(rule_line, rule_model + " requires a nonzero unit_bits "
                                             "or bank register_bits");
                return false;
            }
            if ((!rule.merge_banks.empty() || !rule.member_banks.empty() ||
                 rule.require_natural_alignment) &&
                rule.action != AbiRuleAction::Flatten) {
                fail(rule_line,
                     rule_model +
                         " uses flatten-only merge/alignment properties");
                return false;
            }
            if ((rule.uniform_members || rule.max_members != ~0U) &&
                rule.action != AbiRuleAction::Flatten) {
                fail(rule_line, rule_model + " uses uniform_members or "
                                             "max_members without flatten");
                return false;
            }
            if (rule.uniform_members && !rule.merge_banks.empty()) {
                fail(rule_line, rule_model + " cannot merge uniform members");
                return false;
            }
            std::unordered_set<std::string_view> member_names;
            for (const auto& member_name : rule.member_banks) {
                if (member_name.empty() ||
                    std::none_of(abi.banks.begin(), abi.banks.end(),
                                 [&](const AbiRegisterBank& candidate) {
                                     return candidate.canonical_name ==
                                            member_name;
                                 }) ||
                    !member_names.insert(member_name).second) {
                    fail(rule_line, rule_model + " has an empty, duplicate, "
                                                 "or unknown member bank");
                    return false;
                }
            }
            if (!rule.merge_banks.empty() && rule.unit_bits == 0) {
                fail(rule_line, rule_model + " requires nonzero unit_bits "
                                             "when merge_banks is present");
                return false;
            }
            if (!rule.padding_bank.empty() &&
                (rule.merge_banks.empty() ||
                 std::none_of(abi.banks.begin(), abi.banks.end(),
                              [&](const AbiRegisterBank& candidate) {
                                  return candidate.canonical_name ==
                                         rule.padding_bank;
                              }))) {
                fail(rule_line, rule_model + " names a padding bank without "
                                             "merge_banks or an unknown one");
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
                    fail(rule_line, rule_model + " has an empty, duplicate, "
                                                 "or unknown merge bank");
                    return false;
                }
                if (merge_bank->register_bits < rule.unit_bits) {
                    fail(rule_line, rule_model + " has a merge bank narrower "
                                                 "than unit_bits");
                    return false;
                }
            }
            if (bank != abi.banks.end() &&
                (rule.action == AbiRuleAction::Split ||
                 rule.action == AbiRuleAction::Coerce) &&
                rule.unit_bits > bank->register_bits) {
                fail(rule_line, rule_model + " has unit_bits wider than its "
                                             "register bank");
                return false;
            }
            if ((rule.cursor_alignment != 1 || rule.cursor_alignment_value ||
                 rule.cursor_advance != 0) &&
                !needs_bank && rule.action != AbiRuleAction::Flatten) {
                fail(rule_line, rule_model + " uses cursor policy without a "
                                             "register bank");
                return false;
            }
            std::unordered_set<std::string_view> required_unused_names;
            for (const auto& required : rule.requires_unused_banks) {
                const auto known = std::find_if(
                    abi.banks.begin(), abi.banks.end(),
                    [&](const AbiRegisterBank& candidate) {
                        return candidate.canonical_name == required;
                    });
                if (required.empty() || known == abi.banks.end() ||
                    !required_unused_names.insert(required).second) {
                    fail(rule_line,
                         rule_model + " has an empty, duplicate, or unknown "
                                      "required-unused bank");
                    return false;
                }
            }
            if (bank != abi.banks.end() &&
                rule.carrier_bits > bank->register_bits) {
                fail(rule_line, rule_model + " has carrier_bits wider than "
                                             "its register bank");
                return false;
            }
            if (bank != abi.banks.end() &&
                rule.action == AbiRuleAction::Indirect &&
                bank->register_bits < abi.address_bits) {
                fail(rule_line, rule_model + " uses an indirect bank narrower "
                                             "than address_bits");
                return false;
            }
            if (rule.action == AbiRuleAction::Indirect &&
                rule.unit_bits != 0 && rule.unit_bits % 8U != 0) {
                fail(rule_line, rule_model + " indirect unit_bits must be "
                                             "byte-addressable");
                return false;
            }
        }
        if (abi.stack_layout == AbiStackLayout::Slots &&
            slot_cursors.size() > 1) {
            fail(line, model + " uses positional stack slots but its "
                               "argument banks do not share one cursor");
            return false;
        }
        if (needs_argument_stack &&
            !has_stack_region(AbiStackRegion::Arguments)) {
            fail(line, model + " can place arguments on the stack but "
                               "stack_order omits 'arguments'");
            return false;
        }
        if (needs_result_stack &&
            !has_stack_region(AbiStackRegion::Results)) {
            fail(line, model + " can place results on the stack but "
                               "stack_order omits 'results'");
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
                fail(property_line(properties, "variadic_count_register"),
                     model + " has an invalid variadic count "
                             "cursor/register");
                return false;
            }
        }
        for (std::size_t index = 0; index < abi.variadic_shadows.size();
             ++index) {
            const auto& shadow = abi.variadic_shadows[index];
            const auto* source = bank_named(shadow.source_bank);
            const auto* destination = bank_named(shadow.target_bank);
            if (!source || !destination ||
                source->cursor != destination->cursor ||
                destination->arguments.size() < source->arguments.size()) {
                fail(blocks.variadic_shadows[index].line,
                     model + " variadic shadow '" + shadow.canonical_name +
                         "' requires compatible banks sharing one cursor");
                return false;
            }
        }
        for (const auto& bank_name : abi.variadic_save_banks) {
            const auto* bank = bank_named(bank_name);
            if (!bank || bank->arguments.empty()) {
                fail(property_line(properties, "variadic_save_banks"),
                     model + " variadic_save_banks contains unknown or "
                             "empty bank '" + bank_name + "'");
                return false;
            }
        }
        if (!abi.variadic_home_bank.empty()) {
            const auto* bank = bank_named(abi.variadic_home_bank);
            if (!bank || bank->arguments.empty()) {
                fail(property_line(properties, "variadic_home_bank"),
                     model + " has an invalid variadic home bank");
                return false;
            }
            // A callee home area lies wholly below the incoming arguments.
            if (abi.variadic_home_base < 0 &&
                abi.variadic_home_base +
                        static_cast<std::int64_t>(bank->arguments.size()) *
                            abi.variadic_home_stride >
                    0) {
                fail(property_line(properties, "variadic_home_base"),
                     model + " places variadic home slots on both sides of "
                             "the incoming arguments");
                return false;
            }
        }
        for (std::size_t index = 0; index < abi.variadic_states.size();
             ++index) {
            const auto& state = abi.variadic_states[index];
            if ((!state.cursor.empty() && !cursor_known(state.cursor)) ||
                (state.kind == AbiVariadicStateKind::RegisterSaveAddress &&
                 abi.variadic_save_banks.empty()) ||
                (state.llvm_va_list_offset &&
                 (*state.llvm_va_list_offset >= abi.variadic_va_list_bytes ||
                  abi.variadic_va_list_bytes == 0))) {
                fail(blocks.variadic_states[index].line,
                     model + " has invalid variadic state '" +
                         state.canonical_name + "'");
                return false;
            }
        }
        return true;
    }

    std::optional<OptionValue> option_value(
        const std::string& name, const ModelProperty& property) {
        switch (property.value.kind) {
        case ModelValue::Kind::Boolean: return property.value.boolean;
        case ModelValue::Kind::Integer:
            if (property.value.negative) {
                fail(property.line, "compiler option property '" + name +
                                        "' cannot be negative");
                return std::nullopt;
            }
            return property.value.integer;
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
            if (!name.starts_with("f.") && !name.starts_with("m.") &&
                !name.starts_with("w.")) {
                fail(property.line, "unknown model property '" + name + "'");
                return false;
            }
            auto value = option_value(name, property);
            if (!value) return false;
            result.push_back(
                {name, std::move(*value),
                 position(property.line)});
        }
        std::sort(result.begin(), result.end(),
                  [](const OptionAssignment& left,
                     const OptionAssignment& right) {
                      return left.name < right.name;
                  });
        return true;
    }

    std::optional<OptimizationEntry> make_optimization(
        std::string name, const ModelProperties& properties, unsigned) {
        OptimizationEntry result;
        result.canonical_name = std::move(name);
        result.inherits = names_property(properties, "inherits");
        if (const auto targets = list_property(properties, "targets")) {
            result.targets = *targets;
        }
        if (!collect_option_properties(
                properties, {"inherits", "targets"}, result.options)) {
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
        result.debug = text_property(properties, "debug");
        result.constructors = text_property(properties, "constructors");
        result.destructors = text_property(properties, "destructors");
        if (!floating_environment(properties, result.floating_environment) ||
            !collect_option_properties(
                properties,
                {"default_for", "target", "abi", "mangling", "optimization",
                 "debug", "data_bases", "fp_traps", "fp_denormal_operand",
                 "fp_denormal_result", "constructors", "destructors"},
                result.options)) {
            return std::nullopt;
        }
        // data_bases is the profile spelling of -mdata-bases.
        if (const auto names = list_property(properties, "data_bases")) {
            const auto line = properties.at("data_bases").line;
            if (properties.contains("m.data-bases")) {
                fail(line, "profile sets both data_bases and m.data-bases");
                return std::nullopt;
            }
            std::string value;
            for (const auto& entry : *names) {
                if (entry.empty() || entry.find(',') != std::string::npos) {
                    fail(line, "profile data_bases must list entry names");
                    return std::nullopt;
                }
                value += (value.empty() ? "" : ",") + entry;
            }
            result.options.push_back({"m.data-bases",
                                      value.empty() ? "none" : value,
                                      position(line)});
            std::sort(result.options.begin(), result.options.end(),
                      [](const OptionAssignment& left,
                         const OptionAssignment& right) {
                          return left.name < right.name;
                      });
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    std::optional<DebugEntry> make_debug(
        std::string name, const ModelProperties& properties, unsigned line) {
        if (!known_properties(properties,
                              {"format", "version", "frame_section", "lines",
                               "frames", "variables", "types"})) {
            return std::nullopt;
        }
        DebugEntry result;
        result.canonical_name = std::move(name);
        const auto line_of = [&](const char* property) {
            return properties.at(property).line;
        };
        const auto format = text_property(properties, "format", line);
        if (!format) return std::nullopt;
        if (*format != "dwarf") {
            fail(line_of("format"), "unknown debug format '" + *format +
                                        "'; the implemented format is 'dwarf'");
            return std::nullopt;
        }
        if (const auto version = unsigned_property(properties, "version")) {
            if (*version != 5) {
                fail(line_of("version"),
                     "the dwarf debug format implements version 5");
                return std::nullopt;
            }
            result.version = *version;
        }
        if (const auto section = text_property(properties, "frame_section")) {
            if (*section != "debug_frame" && *section != "eh_frame") {
                fail(line_of("frame_section"),
                     "debug frame_section must be 'debug_frame' or 'eh_frame'");
                return std::nullopt;
            }
            result.eh_frame = *section == "eh_frame";
        }
        const std::pair<const char*, bool*> contents[] = {
            {"lines", &result.lines},
            {"frames", &result.frames},
            {"variables", &result.variables},
            {"types", &result.types},
        };
        for (const auto& [property, flag] : contents) {
            if (const auto value = bool_property(properties, property)) {
                *flag = *value;
            }
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    std::optional<DataBaseEntry> make_data_base(
        std::string name, const ModelProperties& properties, unsigned line) {
        if (!known_properties(properties,
                              {"base", "sections", "max_size", "kinds"})) {
            return std::nullopt;
        }
        DataBaseEntry result;
        result.canonical_name = std::move(name);
        const auto line_of = [&](const char* property) {
            return properties.at(property).line;
        };
        const auto base = text_property(properties, "base", line);
        if (!base) return std::nullopt;
        if (std::none_of(all_targets().begin(), all_targets().end(),
                         [&](const TargetInfo* target) {
                             return find_register(*target, *base) != nullptr;
                         })) {
            fail(line_of("base"), "unknown register '" + *base + "'");
            return std::nullopt;
        }
        result.base = *base;
        result.base_source = position(line_of("base"));
        if (!properties.contains("sections")) {
            fail(line, "missing required model property 'sections'");
            return std::nullopt;
        }
        auto sections = list_property(properties, "sections");
        if (!sections) return std::nullopt;
        if (sections->empty() || sections->size() > 2) {
            fail(line_of("sections"),
                 "data_base sections must name one or two sections");
            return std::nullopt;
        }
        for (const auto& section : *sections) {
            if (section.empty()) {
                fail(line_of("sections"),
                     "data_base section names cannot be empty");
                return std::nullopt;
            }
        }
        if (sections->size() == 2 && sections->front() == sections->back()) {
            fail(line_of("sections"),
                 "data_base sections list '" + sections->front() + "' twice");
            return std::nullopt;
        }
        result.sections = std::move(*sections);
        if (const auto size = unsigned_property(properties, "max_size")) {
            result.max_size = *size;
        }
        if (const auto kinds = list_property(properties, "kinds")) {
            result.writable = false;
            for (const auto& kind : *kinds) {
                bool* flag = kind == "writable" ? &result.writable
                             : kind == "readonly" ? &result.readonly
                             : kind == "literal"  ? &result.literal
                                                  : nullptr;
                if (!flag) {
                    fail(line_of("kinds"),
                         "unknown data_base kind '" + kind +
                             "'; the kinds are writable, readonly, and "
                             "literal");
                    return std::nullopt;
                }
                if (*flag) {
                    fail(line_of("kinds"),
                         "data_base kinds list '" + kind + "' twice");
                    return std::nullopt;
                }
                *flag = true;
            }
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    std::optional<InitTableEntry> make_init_table(
        std::string name, const ModelProperties& properties, unsigned line) {
        if (!known_properties(properties,
                              {"section", "priority_section",
                               "invert_priority", "abi"})) {
            return std::nullopt;
        }
        InitTableEntry result;
        result.canonical_name = std::move(name);
        const auto section = text_property(properties, "section", line);
        if (!section) return std::nullopt;
        result.section = *section;
        result.priority_section = text_property(properties, "priority_section");
        result.invert_priority =
            bool_property(properties, "invert_priority").value_or(false);
        result.abi = text_property(properties, "abi");
        if (!error_.empty()) return std::nullopt;
        if (result.section.empty() ||
            (result.priority_section && result.priority_section->empty()) ||
            (result.abi && result.abi->empty())) {
            fail(line, "init_table section, priority_section, and abi "
                       "cannot be empty");
            return std::nullopt;
        }
        if (result.invert_priority && !result.priority_section) {
            fail(properties.at("invert_priority").line,
                 "init_table invert_priority requires priority_section");
            return std::nullopt;
        }
        return result;
    }

    std::optional<CpuEntry> parse_cpu(std::string name, unsigned entry_line) {
        ModelProperties properties;
        std::vector<AbiNestedBlock> blocks;
        while (current_.kind != ModelTokenKind::RightBrace &&
               current_.kind != ModelTokenKind::End) {
            if (current_.kind != ModelTokenKind::Identifier) {
                fail(current_.line, "expected cpu property or instruction");
                return std::nullopt;
            }
            if (current_.text != "instruction") {
                if (!parse_property(properties)) return std::nullopt;
                continue;
            }
            const auto line = current_.line;
            advance();
            if (current_.kind != ModelTokenKind::String) {
                fail(current_.line, "expected quoted instruction name");
                return std::nullopt;
            }
            auto block_name = current_.text;
            advance();
            if (!expect(ModelTokenKind::LeftBrace,
                        "'{' after instruction name")) {
                return std::nullopt;
            }
            auto block_properties = parse_properties();
            if (!block_properties) return std::nullopt;
            blocks.push_back(
                {std::move(block_name), std::move(*block_properties), line});
        }
        if (!expect(ModelTokenKind::RightBrace, "'}' after cpu entry")) {
            return std::nullopt;
        }
        return make_cpu(std::move(name), properties, blocks, entry_line);
    }

    // An integer cost from `minimum` through max_cpu_cost.
    std::optional<unsigned> cost_property(const ModelProperties& properties,
                                          std::string_view name,
                                          unsigned minimum) {
        const auto value = unsigned_property(properties, name);
        if (value && (*value < minimum || *value > max_cpu_cost)) {
            fail(properties.at(std::string(name)).line,
                 "model property '" + std::string(name) + "' must be from " +
                     std::to_string(minimum) + " to " +
                     std::to_string(max_cpu_cost));
            return std::nullopt;
        }
        return value;
    }

    std::optional<CpuEntry> make_cpu(
        std::string name, const ModelProperties& properties,
        const std::vector<AbiNestedBlock>& blocks, unsigned line) {
        if (!known_properties(properties,
                              {"architecture", "inherits", "features",
                               "issue_width", "function_alignment"})) {
            return std::nullopt;
        }
        CpuEntry result;
        result.canonical_name = std::move(name);
        const auto architecture =
            text_property(properties, "architecture", line);
        if (!architecture) return std::nullopt;
        const auto* target = architecture_target(*architecture);
        if (!target) {
            fail(properties.at("architecture").line,
                 "unknown architecture '" + *architecture + "'");
            return std::nullopt;
        }
        result.architecture = *architecture;
        result.inherits = names_property(properties, "inherits");
        if (auto features = list_property(properties, "features")) {
            if (auto problem = feature_problem(*target, *features)) {
                fail(properties.at("features").line, std::move(*problem));
                return std::nullopt;
            }
            result.features = std::move(*features);
        }
        result.issue_width = cost_property(properties, "issue_width", 1);
        result.function_alignment =
            unsigned_property(properties, "function_alignment");
        if (result.function_alignment &&
            (*result.function_alignment == 0 ||
             *result.function_alignment > 65536 ||
             !std::has_single_bit(*result.function_alignment))) {
            fail(properties.at("function_alignment").line,
                 "model property 'function_alignment' must be a power of two "
                 "from 1 to 65536");
            return std::nullopt;
        }
        for (const auto& block : blocks) {
            if (std::any_of(result.instructions.begin(),
                            result.instructions.end(),
                            [&](const CpuInstructionEntry& entry) {
                                return entry.name == block.name;
                            })) {
                fail(block.line,
                     "duplicate instruction '" + block.name + "'");
                return std::nullopt;
            }
            if (!known_properties(block.properties,
                                  {"latency", "issue", "available"})) {
                return std::nullopt;
            }
            result.instructions.push_back(
                {block.name, block.line, {},
                 cost_property(block.properties, "latency", 0),
                 cost_property(block.properties, "issue", 1),
                 bool_property(block.properties, "available")});
        }
        if (!error_.empty()) return std::nullopt;
        return result;
    }

    bool floating_environment(const ModelProperties& properties,
                              floating::Environment& result) {
        using floating::Exception;
        if (const auto traps = list_property(properties, "fp_traps")) {
            const auto line = properties.find("fp_traps")->second.line;
            for (const auto& name : *traps) {
                std::optional<Exception> found;
                for (const auto exception :
                     {Exception::Invalid, Exception::DivideByZero,
                      Exception::Overflow, Exception::Underflow,
                      Exception::Inexact}) {
                    if (floating::exception_name(exception) == name) {
                        found = exception;
                    }
                }
                if (!found) {
                    fail(line, "profile fp_traps names unknown exception '" +
                                   name + "'");
                    return false;
                }
                if ((result.traps & floating::exception_set(*found)) != 0) {
                    fail(line, "profile fp_traps lists '" + name + "' twice");
                    return false;
                }
                result.traps |= floating::exception_set(*found);
            }
        }
        if (const auto value = text_property(properties, "fp_denormal_operand")) {
            if (*value == "trap") {
                result.traps |=
                    floating::exception_set(Exception::DenormalOperand);
            } else if (*value != "ieee") {
                fail(properties.find("fp_denormal_operand")->second.line,
                     "profile fp_denormal_operand must be 'ieee' or 'trap'");
                return false;
            }
        }
        if (const auto value = text_property(properties, "fp_denormal_result")) {
            if (*value == "flush") {
                result.flush_denormal_results = true;
            } else if (*value != "ieee") {
                fail(properties.find("fp_denormal_result")->second.line,
                     "profile fp_denormal_result must be 'ieee' or 'flush'");
                return false;
            }
        }
        return error_.empty();
    }

    std::string_view text_;
    std::span<const ModelEntrySource> entries_;
    std::size_t next_entry_{};
    ModelLexer lexer_;
    std::string origin_;
    ModelToken current_;
    std::string error_position_;
    std::string error_;
};

// An entry and its bases, each once, by inheritance depth: the entry has
// depth 0 and its direct bases depth 1; a base reached along several paths
// counts once, at its smallest depth. `base` finds a base by name.
template <typename Entry, typename Base>
std::vector<std::pair<const Entry*, unsigned>> inheritance_graph(
    const Entry& entry, Base base) {
    std::vector<std::pair<const Entry*, unsigned>> result{{&entry, 0U}};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto [item, depth] = result[index];
        for (const auto& name : item->inherits) {
            const auto* found = base(*item, name);
            if (found && std::none_of(result.begin(), result.end(),
                                      [&](const auto& seen) {
                                          return seen.first == found;
                                      })) {
                result.emplace_back(found, depth + 1);
            }
        }
    }
    return result;
}

// The inheritance cycle through `entry`, as "a -> b -> a", or nothing.
template <typename Entry, typename Base>
std::optional<std::string> inheritance_cycle(const Entry& entry, Base base) {
    std::vector<const Entry*> path{&entry};
    std::vector<const Entry*> finished;
    const auto visit = [&](const auto& self, const Entry& item) -> bool {
        for (const auto& name : item.inherits) {
            const auto* next = base(item, name);
            if (!next) continue;
            if (next == &entry) {
                path.push_back(next);
                return true;
            }
            if (std::find(path.begin(), path.end(), next) != path.end() ||
                std::find(finished.begin(), finished.end(), next) !=
                    finished.end()) {
                continue;
            }
            path.push_back(next);
            if (self(self, *next)) return true;
            path.pop_back();
            finished.push_back(next);
        }
        return false;
    };
    if (!visit(visit, entry)) return std::nullopt;
    std::string result;
    for (const auto* item : path) {
        result += (result.empty() ? "" : " -> ") + item->canonical_name;
    }
    return result;
}

// Why an entry's nearest bases set different values of something that the
// entry does not set, or nothing. `values(entry)` maps the name of each
// property or block that an entry sets itself to its value.
template <typename Entry, typename Values>
std::optional<std::string> inheritance_conflict(
    const std::vector<std::pair<const Entry*, unsigned>>& graph,
    Values values) {
    using Value = std::invoke_result_t<Values, const Entry&>;
    struct Nearest {
        typename Value::mapped_type value;
        const Entry* owner{};
        unsigned depth{};
    };
    const auto own = values(*graph.front().first);
    std::map<typename Value::key_type, Nearest> nearest;
    for (const auto& [item, depth] : graph) {
        if (depth == 0) continue;
        for (const auto& [key, value] : values(*item)) {
            if (own.contains(key)) continue;
            const auto [found, inserted] =
                nearest.emplace(key, Nearest{value, item, depth});
            if (!inserted && found->second.depth == depth &&
                !(found->second.value == value)) {
                return "inherits different values of " + key + " from '" +
                       found->second.owner->canonical_name + "' and '" +
                       item->canonical_name + "'";
            }
        }
    }
    return std::nullopt;
}

// The costs of an instruction block; omitted properties have the table's.
InstructionCost block_cost(std::span<const InstructionCostEntry> costs,
                           const CpuInstructionEntry& block) {
    const auto& defaults = costs[block.instruction.value];
    return {block.latency.value_or(defaults.latency),
            block.issue.value_or(defaults.issue),
            block.available.value_or(true)};
}

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

} // namespace

ModelRegistry::ModelRegistry(bool load_shipped) {
    if (!load_shipped) return;
    std::ostringstream errors;
    Diagnostics diagnostics(errors);
    for (const auto& source : shipped_model_sources()) {
        std::string text;
        for (const auto piece : source.text) text += piece;
        if (!load_text(text, std::string(source.name), diagnostics)) {
            throw std::logic_error("invalid shipped Cross model: " +
                                   errors.str());
        }
        loaded_files_.insert(std::string(source.name));
    }
    if (!complete_instructions(diagnostics)) {
        throw std::logic_error("invalid shipped Cross model: " + errors.str());
    }
}

const TargetInfo* architecture_target(std::string_view architecture) {
    for (const auto* target : all_targets()) {
        if (target->architecture == architecture) return target;
    }
    return nullptr;
}

bool ModelRegistry::load_text(std::string_view text, std::string origin,
                              Diagnostics& diagnostics,
                              std::optional<std::span<const ModelEntrySource>> entries) {
    if (entries && loaded_files_.contains(origin)) return true;
    const auto indexed = entries ? std::vector<ModelEntrySource>{} : index_model(text);
    std::vector<ModelEntrySource> pending;
    const auto& loaded = loaded_entries_[origin];
    for (const auto& entry : entries.value_or(indexed)) {
        if (!loaded.contains(entry.offset)) pending.push_back(entry);
    }
    ModelParser parser(text, origin, pending);
    auto document = parser.parse();
    if (!document) {
        diagnostics.file_error(parser.error_position(), parser.error());
        return false;
    }

    // Names and private-call candidates are unique per architecture and
    // address width; find_abi resolves a name shared by address widths. An
    // entry that takes its width from the triple uses its names at each
    // width, except an alias that an entry of the same file states for that
    // width.
    const auto abi_key = [](const AbiEntry& entry, std::string_view item) {
        return entry.architecture + '\n' +
               std::to_string(entry.address_bits) + '\n' + std::string(item);
    };
    std::unordered_set<std::string> abi_names;
    std::unordered_set<std::string> private_conventions;
    for (const auto& entry : abis_) {
        abi_names.insert(abi_key(entry, entry.canonical_name));
        for (const auto& alias : entry.aliases) {
            abi_names.insert(abi_key(entry, alias));
        }
        if (entry.private_carrier_bits != 0) {
            private_conventions.insert(abi_key(
                entry, std::to_string(entry.private_carrier_bits)));
        }
    }
    std::unordered_set<std::string> stated_names;
    const auto claim = [&](const AbiEntry& entry) {
        const auto model = " for " + entry.architecture + " with " +
                           std::to_string(entry.address_bits) +
                           "-bit addresses";
        const auto add_name = [&](std::string_view name, bool alias) {
            const auto key = abi_key(entry, name);
            if (entry.address_bits_from_triple && alias &&
                stated_names.contains(key)) {
                return true;
            }
            if (!entry.address_bits_from_triple) stated_names.insert(key);
            if (abi_names.insert(key).second) return true;
            diagnostics.file_error(entry.source,
                                   "duplicate ABI model name or alias '" +
                                       std::string(name) + "'" + model);
            return false;
        };
        if (!add_name(entry.canonical_name, false)) return false;
        for (const auto& alias : entry.aliases) {
            if (!add_name(alias, true)) return false;
        }
        if (entry.private_carrier_bits != 0 &&
            !private_conventions
                 .insert(abi_key(entry,
                                 std::to_string(entry.private_carrier_bits)))
                 .second) {
            diagnostics.file_error(
                entry.source,
                "ABI model '" + entry.canonical_name +
                    "' duplicates the private convention for " +
                    std::to_string(entry.private_carrier_bits) +
                    "-bit carriers" + model);
            return false;
        }
        return true;
    };
    for (const bool from_triple : {false, true}) {
        for (const auto& entry : document->abis) {
            if (entry.address_bits_from_triple == from_triple && !claim(entry)) {
                return false;
            }
        }
    }

    // Mangling, optimization, profile, debug, and data_base names are unique
    // within their kind.
    const auto unique_names = [&](const auto& loaded, const auto& added,
                                  std::string_view kind) {
        std::unordered_set<std::string_view> names;
        for (const auto& entry : loaded) names.insert(entry.canonical_name);
        for (const auto& entry : added) {
            if (names.insert(entry.canonical_name).second) continue;
            diagnostics.file_error(entry.source,
                                   "duplicate " + std::string(kind) +
                                       " model '" + entry.canonical_name +
                                       "'");
            return false;
        }
        return true;
    };
    if (!unique_names(manglings_, document->manglings, "mangling") ||
        !unique_names(optimizations_, document->optimizations,
                      "optimization") ||
        !unique_names(profiles_, document->profiles, "profile") ||
        !unique_names(debugs_, document->debugs, "debug") ||
        !unique_names(data_bases_, document->data_bases, "data_base") ||
        !unique_names(init_tables_, document->init_tables, "init_table")) {
        return false;
    }
    // CPU names are unique per architecture.
    std::unordered_set<std::string> cpu_names;
    for (const auto& entry : cpus_) {
        cpu_names.insert(entry.architecture + '\n' + entry.canonical_name);
    }
    for (const auto& entry : document->cpus) {
        if (cpu_names.insert(entry.architecture + '\n' + entry.canonical_name)
                .second) {
            continue;
        }
        diagnostics.file_error(entry.source,
                               "duplicate " + entry.architecture +
                                   " cpu model '" + entry.canonical_name +
                                   "'");
        return false;
    }

    const auto abi_capacity = static_cast<std::size_t>(AbiId::invalid_value);
    if (abis_.size() > abi_capacity ||
        document->abis.size() > abi_capacity - abis_.size()) {
        diagnostics.file_error(
            origin, "ABI model count exceeds the typed registry limit");
        return false;
    }
    for (const auto& entry : document->abis) {
        if (entry.variadic_states.size() >
            static_cast<std::size_t>(AbiStateId::invalid_value)) {
            diagnostics.file_error(entry.source,
                                   "ABI model '" + entry.canonical_name +
                                       "' has too many variadic-state "
                                       "entries");
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
    for (auto& entry : document->debugs) {
        debugs_.push_back(std::move(entry));
    }
    for (auto& entry : document->cpus) {
        cpus_.push_back(std::move(entry));
    }
    for (auto& entry : document->data_bases) {
        data_bases_.push_back(std::move(entry));
    }
    for (auto& entry : document->init_tables) {
        init_tables_.push_back(std::move(entry));
    }
    std::unordered_map<std::string, std::size_t> instruction_counts;
    for (const auto& entry : document->instructions) ++instruction_counts[entry.architecture];
    for (const auto& [architecture, count] : instruction_counts) {
        auto table = std::find_if(instruction_tables_.begin(), instruction_tables_.end(),
            [&](const InstructionTable& item) { return item.architecture == architecture; });
        if (table == instruction_tables_.end()) {
            InstructionTable added;
            added.architecture = architecture;
            instruction_tables_.push_back(std::move(added));
            table = std::prev(instruction_tables_.end());
        }
        table->entries.reserve(table->entries.size() + count);
    }
    for (auto& entry : document->instructions) {
        auto table = std::find_if(instruction_tables_.begin(),
                                  instruction_tables_.end(),
                                  [&](const InstructionTable& item) {
                                      return item.architecture ==
                                             entry.architecture;
                                  });
        table->entries.push_back(std::move(entry));
        instructions_pending_ = true;
    }
    for (const auto& entry : pending) loaded_entries_[origin].insert(entry.offset);
    if (std::find(origins_.begin(), origins_.end(), origin) == origins_.end()) {
        origins_.push_back(std::move(origin));
    }
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
    if (!load_text(contents.str(), identity, diagnostics)) return false;
    loaded_files_.insert(identity);
    return true;
}

std::vector<const ProfileEntry*> ModelRegistry::default_profiles(
    std::string_view triple) const {
    std::vector<const ProfileEntry*> best;
    std::size_t best_score{};
    for (const auto& profile : profiles_) {
        std::optional<std::size_t> score;
        for (const auto& pattern : profile.default_for) {
            if (!glob_matches(pattern, triple)) continue;
            const auto specificity = static_cast<std::size_t>(std::count_if(
                pattern.begin(), pattern.end(),
                [](char ch) { return ch != '*'; }));
            score = std::max(score.value_or(0), specificity);
        }
        if (!score || (!best.empty() && *score < best_score)) continue;
        if (best.empty() || *score > best_score) best.clear();
        best.push_back(&profile);
        best_score = *score;
    }
    return best;
}

const ProfileEntry* ModelRegistry::default_profile(
    std::string_view triple) const {
    const auto profiles = default_profiles(triple);
    return profiles.size() == 1 ? profiles.front() : nullptr;
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
    // The entries of `architecture` that `name` names, at the address width
    // `address_bits` or, when it is zero, at any width.
    const auto named = [&](unsigned address_bits) {
        std::vector<const AbiEntry*> result;
        for (const auto& entry : abis_) {
            if (entry.architecture == architecture &&
                (address_bits == 0 || entry.address_bits == address_bits) &&
                (entry.canonical_name == name ||
                 std::find(entry.aliases.begin(), entry.aliases.end(),
                           name) != entry.aliases.end())) {
                result.push_back(&entry);
            }
        }
        return result;
    };
    if (const auto entries = named(0); entries.size() == 1) {
        return entries.front();
    }
    // A name shared by several address widths, such as each MIPS width's
    // `x`, names the entry of the triple's width; there an entry that states
    // its width takes precedence over one that takes it from the triple.
    const auto* target = target_for_triple(triple);
    if (!target || target->architecture != architecture) return nullptr;
    const auto entries = named(target->address_bits(triple));
    const auto stated = std::find_if(
        entries.begin(), entries.end(), [](const AbiEntry* entry) {
            return !entry->address_bits_from_triple;
        });
    if (stated != entries.end()) return *stated;
    return entries.size() == 1 ? entries.front() : nullptr;
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

const DebugEntry* ModelRegistry::find_debug(std::string_view name) const {
    for (const auto& entry : debugs_) {
        if (entry.canonical_name == name) return &entry;
    }
    return nullptr;
}

const CpuEntry* ModelRegistry::find_cpu(std::string_view architecture,
                                        std::string_view name) const {
    for (const auto& entry : cpus_) {
        if (entry.architecture == architecture && entry.canonical_name == name) {
            return &entry;
        }
    }
    return nullptr;
}

const DataBaseEntry* ModelRegistry::find_data_base(
    std::string_view name) const {
    for (const auto& entry : data_bases_) {
        if (entry.canonical_name == name) return &entry;
    }
    return nullptr;
}

const InitTableEntry* ModelRegistry::find_init_table(
    std::string_view name) const {
    for (const auto& entry : init_tables_) {
        if (entry.canonical_name == name) return &entry;
    }
    return nullptr;
}

ResolvedCpu ModelRegistry::resolve_cpu(const TargetInfo& target,
                                       const CpuEntry& entry) const {
    const auto base = [&](const CpuEntry& item, std::string_view name) {
        return find_cpu(item.architecture, name);
    };
    ResolvedCpu result;
    // The features are the entry's own or the union of its bases', in order.
    std::vector<const CpuEntry*> visited;
    const auto collect = [&](const auto& self, const CpuEntry& item) -> void {
        if (std::find(visited.begin(), visited.end(), &item) != visited.end()) {
            return;
        }
        visited.push_back(&item);
        if (!item.features) {
            for (const auto& name : item.inherits) {
                if (const auto* next = base(item, name)) self(self, *next);
            }
            return;
        }
        for (const auto& feature : *item.features) {
            if (std::find(result.features.begin(), result.features.end(),
                          feature) == result.features.end()) {
                result.features.push_back(feature);
            }
        }
    };
    collect(collect, entry);
    const auto costs = instruction_costs(target);
    for (const auto& instruction : costs) {
        result.instructions.push_back(
            {instruction.latency, instruction.issue, true});
    }
    // Other values come from the nearest entry that sets them, and loading
    // rejected nearest entries that disagree, so deeper entries apply first.
    const auto graph = inheritance_graph(entry, base);
    for (auto item = graph.rbegin(); item != graph.rend(); ++item) {
        const auto& cpu = *item->first;
        if (cpu.issue_width) result.issue_width = *cpu.issue_width;
        if (cpu.function_alignment) {
            result.function_alignment = cpu.function_alignment;
        }
        for (const auto& block : cpu.instructions) {
            result.instructions[block.instruction.value] =
                block_cost(costs, block);
        }
    }
    return result;
}

std::vector<OptionAssignment> ModelRegistry::resolve_optimization(
    const OptimizationEntry& entry) const {
    // Each option comes from the nearest entry that sets it, and loading
    // rejected nearest entries that disagree, so deeper entries apply first.
    const auto graph = inheritance_graph(
        entry, [&](const OptimizationEntry&, std::string_view name) {
            return find_optimization(name);
        });
    std::map<std::string, OptionAssignment> settings;
    for (auto item = graph.rbegin(); item != graph.rend(); ++item) {
        for (const auto& setting : item->first->options) {
            settings.insert_or_assign(setting.name, setting);
        }
    }
    std::vector<OptionAssignment> result;
    for (auto& [name, setting] : settings) result.push_back(std::move(setting));
    return result;
}

ModelRegistry& model_registry(bool load_shipped) {
    static ModelRegistry registry(load_shipped);
    static const bool installed = (registry.install_instruction_tables(), true);
    (void)installed;
    return registry;
}

namespace {

const OptionDefinition* option_definition(
    std::span<const OptionDefinition> definitions, std::string_view name) {
    const auto found = std::find_if(
        definitions.begin(), definitions.end(),
        [&](const OptionDefinition& definition) {
            return definition.name == name;
        });
    return found == definitions.end() ? nullptr : &*found;
}

// Why an f.* or m.* property does not suit `definitions`, or nothing.
std::optional<std::string> option_problem(
    const OptionAssignment& option,
    std::span<const OptionDefinition> definitions, bool preset) {
    const auto* definition = option_definition(definitions, option.name);
    if (!definition) return "unknown option '" + option.name + "'";
    if (preset && !definition->presettable) {
        return "optimization presets cannot set option '" + option.name +
               "'";
    }
    std::string reason;
    if (!checked_option_value(*definition, option.value, reason)) {
        return "invalid value for option '" + option.name + "': " + reason;
    }
    return std::nullopt;
}

// Checks the f.*, m.*, and w.* properties of a preset or profile. An m.* property
// must suit each of `targets`, or, when `targets` is empty, some compiled-in
// target that defines it.
bool check_options(std::span<const OptionAssignment> options,
                   std::span<const TargetInfo* const> targets, bool preset,
                   Diagnostics& diagnostics) {
    for (const auto& option : options) {
        std::optional<std::string> problem;
        if (option.name.starts_with("f.") || option.name.starts_with("w.") ||
            option_definition(common_option_definitions(), option.name)) {
            problem =
                option_problem(option, common_option_definitions(), preset);
        } else if (!targets.empty()) {
            for (const auto* target : targets) {
                problem = option_problem(option, target->options, preset);
                if (problem) {
                    *problem += " for " + std::string(target->architecture);
                    break;
                }
            }
        } else {
            problem = "unknown option '" + option.name + "'";
            for (const auto* target : all_targets()) {
                if (!option_definition(target->options, option.name)) continue;
                problem = option_problem(option, target->options, preset);
                if (!problem) break;
            }
        }
        if (problem) {
            diagnostics.file_error(option.source, *problem);
            return false;
        }
    }
    return true;
}

// An m.arch or m.tune property names a cpu entry of each of `targets`, or,
// when `targets` is empty, of some compiled-in target; m.arch may add
// feature modifiers.
bool check_cpu_options(const ModelRegistry& registry,
                       std::span<const OptionAssignment> options,
                       std::span<const TargetInfo* const> targets,
                       Diagnostics& diagnostics,
                       std::span<const IndexedModel> sources,
                       std::optional<std::string_view> architecture) {
    for (const auto& option : options) {
        const auto* name = std::get_if<std::string>(&option.value);
        if ((option.name != "m.arch" && option.name != "m.tune") || !name) {
            continue;
        }
        std::string problem;
        const auto known = [&](const TargetInfo* target) {
            if (architecture && target->architecture != *architecture) {
                return indexed_cpu(sources, target->architecture, *name,
                                   option.name == "m.arch");
            }
            if (option.name == "m.tune") {
                return registry.find_cpu(target->architecture, *name) != nullptr;
            }
            return parse_cpu_selection(*target, *name, problem).has_value();
        };
        const auto candidates =
            targets.empty() ? std::span<const TargetInfo* const>(all_targets())
                            : targets;
        if (targets.empty() ? std::none_of(candidates.begin(),
                                           candidates.end(), known)
                            : !std::all_of(candidates.begin(),
                                           candidates.end(), known)) {
            diagnostics.file_error(
                option.source,
                !problem.empty() ? problem + " for option '" + option.name + "'"
                                 : "unknown cpu '" + *name + "' for option '" +
                                       option.name + "'");
            return false;
        }
    }
    return true;
}

// Checks the names that presets and profiles use, once every file is loaded:
// parents, architectures, targets, ABIs, manglings, presets, and options.
bool check_entries(const ModelRegistry& registry, Diagnostics& diagnostics,
                   std::span<const IndexedModel> sources,
                   std::optional<std::string_view> architecture) {
    for (const auto& table : registry.init_tables()) {
        if (table.abi && !indexed_abi(sources, {}, *table.abi, true) &&
            std::none_of(registry.abis().begin(), registry.abis().end(),
                         [&](const AbiEntry& abi) {
                             return abi.function_selectable &&
                                    (abi.canonical_name == *table.abi ||
                                     std::find(abi.aliases.begin(),
                                               abi.aliases.end(),
                                               *table.abi) !=
                                         abi.aliases.end());
                         })) {
            diagnostics.file_error(table.source,
                                   "init_table '" + table.canonical_name +
                                       "' names unknown or not "
                                       "function-selectable ABI '" +
                                       *table.abi + "'");
            return false;
        }
    }
    const auto& presets = registry.optimizations();
    const auto preset_base = [&](const OptimizationEntry&,
                                 std::string_view name) {
        return registry.find_optimization(name);
    };
    const auto preset_failure = [&](const OptimizationEntry& preset,
                                    std::string message) {
        diagnostics.file_error(preset.source, "optimization preset '" +
                                                  preset.canonical_name +
                                                  "' " + std::move(message));
        return false;
    };
    for (const auto& preset : presets) {
        const auto fail = [&](std::string message) {
            return preset_failure(preset, std::move(message));
        };
        for (const auto& name : preset.inherits) {
            if (!registry.find_optimization(name)) {
                return fail("inherits unknown preset '" + name + "'");
            }
        }
        if (const auto cycle = inheritance_cycle(preset, preset_base)) {
            diagnostics.file_error(preset.source,
                                   "optimization inheritance cycle: " + *cycle);
            return false;
        }
        std::vector<const TargetInfo*> targets;
        for (const auto& name : preset.targets) {
            const auto* target = architecture_target(name);
            if (!target) {
                return fail("names unknown architecture '" + name +
                            "'");
            }
            targets.push_back(target);
        }
        if (!check_options(preset.options, targets, true, diagnostics) ||
            !check_cpu_options(registry, preset.options, targets,
                               diagnostics, sources, architecture)) {
            return false;
        }
    }
    // The nearest bases that set an option agree, unless the entry sets it.
    for (const auto& preset : presets) {
        const auto problem = inheritance_conflict(
            inheritance_graph(preset, preset_base),
            [](const OptimizationEntry& entry) {
                std::map<std::string, OptionValue> result;
                for (const auto& setting : entry.options) {
                    result.emplace("'" + setting.name + "'", setting.value);
                }
                return result;
            });
        if (problem) return preset_failure(preset, *problem);
    }
    for (const auto& profile : registry.profiles()) {
        const auto fail = [&](std::string message) {
            diagnostics.file_error(profile.source,
                                   "profile '" + profile.canonical_name +
                                       "' " + std::move(message));
            return false;
        };
        const TargetInfo* target{};
        if (profile.target) {
            target = target_for_triple(*profile.target);
            if (!target) {
                return fail("names unimplemented target '" +
                            *profile.target + "'");
            }
        }
        if (profile.abi &&
            !indexed_abi(sources, target ? target->architecture : std::string_view{},
                         *profile.abi) &&
            std::none_of(registry.abis().begin(), registry.abis().end(),
                         [&](const AbiEntry& abi) {
                             return (!target || abi.architecture ==
                                                    target->architecture) &&
                                    (abi.canonical_name == *profile.abi ||
                                     std::find(abi.aliases.begin(),
                                               abi.aliases.end(),
                                               *profile.abi) !=
                                         abi.aliases.end());
                         })) {
            return fail("names unknown ABI '" + *profile.abi + "'" +
                        (target ? " for " + std::string(target->architecture)
                                : std::string{}));
        }
        if (profile.mangling &&
            std::none_of(registry.manglings().begin(),
                         registry.manglings().end(),
                         [&](const ManglingEntry& mangling) {
                             return mangling.canonical_name ==
                                    *profile.mangling;
                         })) {
            return fail("names unknown mangling '" + *profile.mangling + "'");
        }
        if (profile.optimization &&
            !registry.find_optimization(*profile.optimization)) {
            return fail("names unknown optimization preset '" +
                        *profile.optimization + "'");
        }
        if (profile.debug && !registry.find_debug(*profile.debug)) {
            return fail("names unknown debug entry '" + *profile.debug + "'");
        }
        for (const auto* name : {&profile.constructors, &profile.destructors}) {
            if (!*name) continue;
            const auto* table = registry.find_init_table(**name);
            if (!table) {
                return fail("names unknown init_table '" + **name + "'");
            }
            if (table->abi && target &&
                !(architecture && target->architecture != *architecture &&
                  indexed_abi(sources, target->architecture, *table->abi)) &&
                !registry.find_abi(target->architecture, *table->abi,
                                   *profile.target)) {
                return fail("names init_table '" + **name +
                            "', whose ABI '" + *table->abi +
                            "' is not an ABI of " +
                            std::string(target->architecture));
            }
        }
        const std::span<const TargetInfo* const> targets =
            target ? std::span<const TargetInfo* const>(&target, 1)
                   : std::span<const TargetInfo* const>{};
        if (!check_options(profile.options, targets, false, diagnostics) ||
            !check_cpu_options(registry, profile.options, targets,
                               diagnostics, sources, architecture)) {
            return false;
        }
    }
    const auto cpu_base = [&](const CpuEntry& item, std::string_view name) {
        return registry.find_cpu(item.architecture, name);
    };
    const auto cpu_failure = [&](const CpuEntry& cpu, std::string message) {
        diagnostics.file_error(cpu.source, "cpu '" + cpu.canonical_name +
                                               "' " + std::move(message));
        return false;
    };
    for (const auto& cpu : registry.cpus()) {
        for (const auto& name : cpu.inherits) {
            if (registry.find_cpu(cpu.architecture, name)) continue;
            for (const auto& other : registry.cpus()) {
                if (other.canonical_name == name) {
                    return cpu_failure(cpu, "inherits '" + name +
                                                "' of architecture " +
                                                other.architecture + ", not " +
                                                cpu.architecture);
                }
            }
            for (const auto& source : sources) {
                for (const auto& other : source.entries) {
                    if (other.domain == "cpu" && other.name == name) {
                        return cpu_failure(cpu, "inherits '" + name +
                                                    "' of architecture " +
                                                    other.architecture + ", not " +
                                                    cpu.architecture);
                    }
                }
            }
            return cpu_failure(cpu, "inherits unknown cpu '" + name + "'");
        }
        if (const auto cycle = inheritance_cycle(cpu, cpu_base)) {
            diagnostics.file_error(cpu.source, "cpu inheritance cycle: " + *cycle);
            return false;
        }
    }
    // The nearest bases that set issue_width or function_alignment or
    // describe an instruction agree unless the entry sets it, and the union
    // of the bases' features is consistent.
    for (const auto& cpu : registry.cpus()) {
        if (cpu.inherits.empty()) continue;
        const auto& target = *architecture_target(cpu.architecture);
        const auto graph = inheritance_graph(cpu, cpu_base);
        auto problem = inheritance_conflict(graph, [](const CpuEntry& entry) {
            std::map<std::string, unsigned> result;
            if (entry.issue_width) result.emplace("'issue_width'", *entry.issue_width);
            if (entry.function_alignment) {
                result.emplace("'function_alignment'", *entry.function_alignment);
            }
            return result;
        });
        if (!problem) {
            const auto costs = registry.instruction_costs(target);
            problem = inheritance_conflict(graph, [&](const CpuEntry& entry) {
                std::map<std::string, std::tuple<unsigned, unsigned, bool>>
                    result;
                for (const auto& block : entry.instructions) {
                    const auto cost = block_cost(costs, block);
                    result.emplace("instruction '" + block.name + "'",
                                   std::tuple{cost.latency, cost.issue,
                                              cost.available});
                }
                return result;
            });
        }
        if (problem) return cpu_failure(cpu, *problem);
        if (cpu.features) continue;
        if (const auto features = feature_problem(
                target, registry.resolve_cpu(target, cpu).features)) {
            return cpu_failure(cpu, "inherits conflicting features: " + *features);
        }
    }
    return true;
}

// Resolves -mdata-bases into the contents of the entries it selects.
bool select_data_bases(const ModelRegistry& registry, const TargetInfo& target,
                       const AbiEntry& abi, CompilerOptions& options,
                       Diagnostics& diagnostics) {
    options.data_bases.clear();
    const auto value = std::string(resolved_text(options, "m.data-bases"));
    if (value == "none") return true;
    const auto* resolved = find_resolved_option(options, "m.data-bases");
    const auto fail = [&](const std::string& message) {
        diagnostics.command_error(message + " from " + resolved->source);
        return false;
    };
    std::vector<const DataBaseEntry*> entries;
    for (std::size_t begin = 0; begin <= value.size();) {
        const auto end = std::min(value.find(',', begin), value.size());
        const auto name = value.substr(begin, end - begin);
        begin = end + 1;
        if (name.empty() || name == "none") {
            return fail("-mdata-bases expects 'none' or data_base entry "
                        "names separated by commas");
        }
        const auto* entry = registry.find_data_base(name);
        if (!entry) return fail("unknown data_base entry '" + name + "'");
        for (const auto* other : entries) {
            if (other == entry) {
                return fail("-mdata-bases lists '" + name + "' twice");
            }
            for (const auto& section : entry->sections) {
                if (std::find(other->sections.begin(), other->sections.end(),
                              section) != other->sections.end()) {
                    return fail("data_base entries '" + other->canonical_name +
                                "' and '" + name + "' both name section '" +
                                section + "'");
                }
            }
        }
        if (target.data_base_registers.empty()) {
            return fail("target " + std::string(target.architecture) +
                        " has no base-relative data addressing; data_base '" +
                        name + "'");
        }
        const auto& bases = target.data_base_registers;
        if (std::find(bases.begin(), bases.end(), entry->base) ==
            bases.end()) {
            std::string names;
            for (const auto base : bases) {
                names += (names.empty() ? "'" : ", '") + std::string(base) +
                         "'";
            }
            diagnostics.file_error(
                entry->base_source,
                "data_base '" + name + "' base register '" + entry->base +
                    "' cannot hold a data base on " +
                    std::string(target.architecture) + "; it uses " + names);
            return false;
        }
        if (abi_carries_register(target, abi, entry->base)) {
            diagnostics.file_error(
                entry->base_source,
                "data_base '" + name + "' base register '" + entry->base +
                    "' carries arguments or results in ABI '" +
                    abi.canonical_name + "'");
            return false;
        }
        entries.push_back(entry);
    }
    for (const auto* entry : entries) {
        options.data_bases.push_back(
            {entry->canonical_name, entry->base, entry->sections,
             entry->max_size, entry->writable, entry->readonly,
             entry->literal});
    }
    return true;
}

} // namespace

bool configure_models(CompilerOptions& options, Diagnostics& diagnostics) {
    auto& registry = model_registry(false);
    std::vector<IndexedModel> sources;
    for (const auto& shipped : shipped_model_sources()) {
        IndexedModel source;
        source.origin = shipped.name;
        for (const auto piece : shipped.text) source.text += piece;
        source.entries = index_model(source.text);
        sources.push_back(std::move(source));
    }
    for (const auto& path : options.model_paths) {
        std::error_code error;
        if (!std::filesystem::is_directory(path, error)) {
            diagnostics.command_error(
                "Cross model search path is not a directory: '" +
                path.string() + "'");
            return false;
        }
    }
    std::unordered_set<std::string> files;
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
        const auto identity = normalized(*resolved);
        if (!files.insert(identity).second || registry.loaded_files_.contains(identity)) continue;
        std::ifstream input(*resolved, std::ios::binary);
        if (!input) {
            diagnostics.command_error("cannot read Cross model file '" + resolved->string() + "'");
            return false;
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        IndexedModel source{contents.str(), identity, {}};
        source.entries = index_model(source.text);
        sources.push_back(std::move(source));
    }
    for (const auto& source : sources) {
        std::vector<ModelEntrySource> profiles;
        for (const auto& entry : source.entries) {
            if (entry.domain == "profile") profiles.push_back(entry);
        }
        if (!registry.load_text(source.text, source.origin, diagnostics, profiles)) return false;
    }
    const auto* requested_profile =
        options.profile_explicit && options.profile != "default"
            ? registry.find_profile(options.profile) : nullptr;
    const auto triple = !options.target_explicit && requested_profile && requested_profile->target
                            ? std::string_view(*requested_profile->target)
                            : std::string_view(options.target);
    const auto* requested_target = target_for_triple(triple);
    const bool all = options.print_targets || options.print_models || options.print_profiles ||
                     options.print_manglings || options.print_optimizations ||
                     options.print_data_bases || options.print_init_tables ||
                     (options.print_cpus && !options.target_explicit);
    const std::optional<std::string_view> architecture =
        all ? std::nullopt : std::optional<std::string_view>(
                                requested_target ? requested_target->architecture : std::string_view{});
    for (const auto& source : sources) {
        std::vector<ModelEntrySource> selected;
        for (const auto& entry : source.entries) {
            if (entry.domain == "profile") continue;
            if (!architecture || entry.architecture.empty() ||
                entry.architecture == *architecture || !architecture_target(entry.architecture)) {
                selected.push_back(entry);
            }
        }
        if (!registry.load_text(source.text, source.origin, diagnostics, selected)) return false;
    }
    if (!registry.complete_instructions(diagnostics, architecture)) return false;
    registry.install_instruction_tables();
    if (!check_entries(registry, diagnostics, sources, architecture)) return false;

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
    }
    // The default profile also fixes the triple's default ABI, so an
    // ambiguous default is an error even when -mprofile names a profile.
    const auto defaults = registry.default_profiles(options.target);
    if (defaults.size() > 1) {
        std::string names;
        for (std::size_t index = 0; index < defaults.size(); ++index) {
            if (index != 0) {
                names += index + 1 < defaults.size() ? ", "
                         : defaults.size() > 2      ? ", and "
                                                    : " and ";
            }
            names += "'" + defaults[index]->canonical_name + "' (" +
                     defaults[index]->source + ")";
        }
        diagnostics.command_error("profiles " + names +
                                  " are equally specific defaults for "
                                  "target '" + options.target + "'");
        return false;
    }
    if (!profile && !defaults.empty()) profile = defaults.front();
    if (!profile && options.profile_explicit) {
        diagnostics.command_error(
            "no default Cross model profile matches target '" +
            options.target + "'");
        return false;
    }
    if (profile) {
        options.profile = profile->canonical_name;
        options.floating_environment = profile->floating_environment;
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

    // The targets of the preset and of each base restrict its use, and its
    // m.* settings need such a restriction.
    bool restricted{};
    for (const auto& [entry, depth] : inheritance_graph(
             *optimization,
             [&](const OptimizationEntry&, std::string_view name) {
                 return registry.find_optimization(name);
             })) {
        const auto& targets = entry->targets;
        if (targets.empty()) continue;
        restricted = true;
        if (!selected_target ||
            std::find(targets.begin(), targets.end(),
                      selected_target->architecture) == targets.end()) {
            diagnostics.command_error(
                "optimization preset '" + entry->canonical_name +
                "' is unavailable for target '" + options.target + "'");
            return false;
        }
    }
    const auto preset_options = registry.resolve_optimization(*optimization);
    for (const auto& setting : preset_options) {
        if (!restricted && setting.name.starts_with("m.")) {
            diagnostics.file_error(
                optimization->source,
                "optimization preset '" + optimization->canonical_name +
                    "' sets '" + setting.name +
                    "', so it or a base needs a targets restriction");
            return false;
        }
    }

    const std::span<const OptionDefinition> target_definitions =
        selected_target
            ? std::span<const OptionDefinition>(selected_target->options)
            : std::span<const OptionDefinition>{};
    const std::span<const OptionAssignment> profile_options =
        profile ? std::span<const OptionAssignment>(profile->options)
                : std::span<const OptionAssignment>{};
    for (auto& option : options.command_options) {
        if (option.name != "g") continue;
        if (const auto* enabled = std::get_if<bool>(&option.value)) {
            option.value = !*enabled ? std::string()
                           : profile && profile->debug ? *profile->debug
                                                       : std::string("dwarf");
        }
    }
    if (!resolve_registered_options(
            options, target_definitions, preset_options, profile_options,
            diagnostics)) {
        return false;
    }
    if (const auto debug = resolved_text(options, "g"); !debug.empty()) {
        const auto* entry = registry.find_debug(debug);
        if (!entry) {
            diagnostics.command_error("unknown debug entry '" +
                                      std::string(debug) + "' from " +
                                      find_resolved_option(options, "g")->source);
            return false;
        }
        options.debug_info = DebugInfoOptions{
            entry->eh_frame, entry->lines, entry->frames, entry->variables,
            entry->types};
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
        if (!select_data_bases(registry, *target, *abi, options,
                               diagnostics)) {
            return false;
        }
        // The profile's initializer tables, which check_entries found, with
        // their ABIs resolved for the target.
        const auto select_table = [&](const std::optional<std::string>& name,
                                      std::optional<InitTableOptions>& selected) {
            selected.reset();
            if (!profile || !name) return true;
            const auto& table = *registry.find_init_table(*name);
            InitTableOptions result{table.canonical_name, table.section,
                                    table.priority_section.value_or(""),
                                    table.invert_priority, {}};
            if (table.abi) {
                const auto* call_abi = registry.find_abi(
                    target->architecture, *table.abi, options.target);
                if (!call_abi || !call_abi->function_selectable) {
                    diagnostics.file_error(
                        table.source,
                        "init_table '" + table.canonical_name + "' names ABI '" +
                            *table.abi + "', which is not a function-selectable "
                            "ABI of target '" + options.target + "'");
                    return false;
                }
                result.abi = call_abi->canonical_name;
            }
            selected = std::move(result);
            return true;
        };
        if (!select_table(profile ? profile->constructors : std::nullopt,
                          options.constructor_table) ||
            !select_table(profile ? profile->destructors : std::nullopt,
                          options.destructor_table)) {
            return false;
        }
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
