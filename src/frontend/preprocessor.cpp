// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/preprocessor.hpp"

#include "frontend/ast.hpp"
#include "frontend/preprocessor_expression.hpp"
#include "frontend/semantic.hpp"
#include "model/model.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace cross {
namespace {

std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) text.remove_suffix(1);
    return std::string(text);
}

bool identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '$';
}

bool identifier_continue(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_' || ch == '$' || ch == ':';
}

std::string normalized(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return (error ? path.lexically_normal() : canonical).generic_string();
}

struct PreprocessingLine {
    std::string text;
    SourceLocation location;
};

// Splicing precedes comment removal. Keep original positions even though
// includes and continued directive lines later form one flattened stream.
std::vector<PreprocessingLine> preprocessing_lines(const SourceFile& file,
                                                 Diagnostics& diagnostics) {
    std::vector<PreprocessingLine> result;
    PreprocessingLine current{{}, {&file, 0, 1, 1}};
    unsigned line = 1;
    const auto& text = file.text;
    for (std::size_t i = 0; i < text.size();) {
        const auto ch = text[i];
        const auto next = i + 1 < text.size() ? text[i + 1] : '\0';
        if (ch == '\\' && (next == '\n' ||
            (next == '\r' && i + 2 < text.size() && text[i + 2] == '\n'))) {
            i += next == '\n' ? 2 : 3;
            ++line;
            continue;
        }
        if (ch == '\n' || (ch == '\r' && next == '\n')) {
            i += ch == '\r' && next == '\n' ? 2 : 1;
            result.push_back(std::move(current));
            current = {{}, {&file, i, ++line, 1}};
            continue;
        }
        current.text.push_back(ch);
        ++i;
    }
    if (!current.text.empty()) result.push_back(std::move(current));
    bool block_comment = false;
    SourceLocation comment_location;
    for (auto& item : result) {
        const auto raw = std::move(item.text);
        item.text.clear();
        char quote = '\0';
        for (std::size_t i = 0; i < raw.size();) {
            const auto ch = raw[i];
            const auto next = i + 1 < raw.size() ? raw[i + 1] : '\0';
            if (block_comment) {
                if (ch == '*' && next == '/') { block_comment = false; i += 2; }
                else ++i;
                continue;
            }
            if (quote != '\0') {
                item.text.push_back(ch);
                ++i;
                if (ch == '\\' && i < raw.size()) item.text.push_back(raw[i++]);
                else if (ch == quote) quote = '\0';
                continue;
            }
            if (ch == '/' && (next == '/' || next == '*')) {
                item.text.push_back(' ');
                if (next == '/') break;
                block_comment = true;
                comment_location = item.location;
                comment_location.column += static_cast<unsigned>(i);
                comment_location.offset += i;
                i += 2;
                continue;
            }
            if (ch == '\'' || ch == '"') quote = ch;
            item.text.push_back(ch);
            ++i;
        }
    }
    if (block_comment) diagnostics.error(comment_location, "unterminated block comment");
    return result;
}

struct Directive {
    std::string name;
    std::string operand;
};

Directive directive(std::string_view text) {
    auto body = trim(text.substr(1));
    std::size_t end = 0;
    while (end < body.size() && identifier_continue(body[end])) ++end;
    return {body.substr(0, end), trim(std::string_view(body).substr(end))};
}

std::optional<std::size_t> top_level_comma(std::string_view text) {
    unsigned parentheses = 0;
    char quote = 0;
    for (std::size_t index = 0; index < text.size(); ++index) {
        const char ch = text[index];
        if (quote) {
            if (ch == '\\' && index + 1 < text.size()) ++index;
            else if (ch == quote) quote = 0;
        } else if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '(') {
            ++parentheses;
        } else if (ch == ')' && parentheses != 0) {
            --parentheses;
        } else if (ch == ',' && parentheses == 0) {
            return index;
        }
    }
    return std::nullopt;
}

std::optional<std::string> require_reason(std::string_view text) {
    const auto literal = trim(text);
    if (literal.size() < 2 || literal.front() != '"') return std::nullopt;
    for (std::size_t index = 1; index < literal.size(); ++index) {
        if (literal[index] == '\\' && index + 1 < literal.size()) {
            ++index;
        } else if (literal[index] == '"') {
            if (index != literal.size() - 1) return std::nullopt;
            return decode_string_literal(literal);
        }
    }
    return std::nullopt;
}

bool valid_macro_name(std::string_view text) {
    if (text.starts_with("$::")) text.remove_prefix(3);
    for (;;) {
        if (text.empty() || !(std::isalpha(static_cast<unsigned char>(text.front())) ||
                              text.front() == '_')) return false;
        std::size_t end = 1;
        while (end < text.size() &&
               (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) ++end;
        text.remove_prefix(end);
        if (text.empty()) return true;
        if (!text.starts_with("::")) return false;
        text.remove_prefix(2);
    }
}

} // namespace

Preprocessor::Preprocessor(SourceManager& sources, Diagnostics& diagnostics,
                           const CompilerOptions& options)
    : sources_(sources), diagnostics_(diagnostics), options_(options) {
    install_predefined_macros();
    define_command_line_macros();
}

void Preprocessor::install_predefined_macros() {
    const auto* target = target_for_triple(options_.target);
    const auto* abi = target
        ? find_abi(*target, options_.abi, options_.target)
        : nullptr;
    const auto pointer_bytes = abi && abi->address_bits != 0
                                   ? (abi->address_bits + 7U) / 8U
                                   : 8U;
    macros_["$::language::version"].replacement = "800i64";
    macros_["$::language::version_major"].replacement = "0";
    macros_["$::language::version_minor"].replacement = "8";
    macros_["$::source::file"].replacement = "\"\"";
    macros_["$::source::line"].replacement = "0";
    macros_["$::target::triple"].replacement = '"' + options_.target + '"';
    macros_["$::target::abi"].replacement = '"' + options_.abi + '"';
    macros_["$::target::mangling"].replacement =
        '"' + options_.mangling + '"';
    macros_["$::target::profile"].replacement =
        '"' + options_.profile + '"';
    macros_["$::target::byte_bits"].replacement = "8";
    macros_["$::target::pointer_bytes"].replacement =
        std::to_string(pointer_bytes);
    macros_["$::target::order_little"].replacement = "1234";
    macros_["$::target::order_big"].replacement = "4321";
    macros_["$::target::byte_order"].replacement =
        target && target->data_layout.byte_order == ByteOrder::Big
            ? "4321"
            : "1234";
    for (const auto* query : {"$::has_builtin", "$::has_intrinsic", "$::has_instruction",
                              "$::has_patch_value", "$::has_patch_operand",
                              "$::has_attribute", "$::has_feature", "$::has_extension",
                              "$::has_abi", "$::has_mangling",
                              "$::has_profile", "$::has_include"}) {
        macros_[query].function_like = true;
    }
}

void Preprocessor::define_command_line_macros() {
    for (const auto& definition : options_.macro_definitions) {
        const auto equals = definition.find('=');
        const auto name = definition.substr(0, equals);
        if (!valid_macro_name(name) || name.starts_with("$::")) {
            diagnostics_.command_error("-D requires a non-builtin macro name");
            continue;
        }
        macros_[name].replacement =
            equals == std::string::npos ? "1" : definition.substr(equals + 1);
    }
    for (const auto& name : options_.macro_undefinitions) {
        if (!valid_macro_name(name) || name.starts_with("$::"))
            diagnostics_.command_error("-U requires a non-builtin macro name");
        else macros_.erase(name);
    }
}

std::filesystem::path Preprocessor::find_include(
    const std::filesystem::path& including, std::string_view name,
    bool quoted) const {
    if (quoted) {
        auto candidate = including.parent_path() / std::string(name);
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    for (const auto& directory : options_.include_paths) {
        auto candidate = directory / std::string(name);
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    for (const auto& directory : options_.system_include_paths) {
        auto candidate = directory / std::string(name);
        if (std::filesystem::is_regular_file(candidate)) return candidate;
    }
    return {};
}

std::string Preprocessor::expand_includes(const std::filesystem::path& path,
                                          std::vector<std::filesystem::path>& stack) {
    const auto identity = normalized(path);
    if (pragma_once_files_.contains(identity) && already_included_.contains(identity)) return {};
    if (std::any_of(stack.begin(), stack.end(), [&](const auto& item) { return normalized(item) == identity; })) {
        diagnostics_.command_error("include cycle involving '" + path.string() + "'");
        return {};
    }
    std::string error;
    const auto* file = sources_.load(path, error);
    if (!file) {
        diagnostics_.command_error(error);
        return {};
    }
    stack.push_back(path);
    already_included_.insert(identity);
    if (dependency_identities_.insert(identity).second) dependencies_.push_back(path);
    std::ostringstream output;
    for (const auto& line : preprocessing_lines(*file, diagnostics_)) {
        const auto stripped = trim(line.text);
        const auto command = stripped.starts_with('#') ? directive(stripped) : Directive{};
        if (command.name == "pragma" && command.operand == "once") {
            pragma_once_files_.insert(identity);
            continue;
        }
        if (command.name == "include") {
            auto operand = command.operand;
            const bool quoted = operand.size() >= 2 && operand.front() == '"' && operand.back() == '"';
            const bool angled = operand.size() >= 2 && operand.front() == '<' && operand.back() == '>';
            if (!quoted && !angled) {
                diagnostics_.error(line.location, "include operand must be a literal path");
                continue;
            }
            operand = operand.substr(1, operand.size() - 2);
            const auto included = find_include(path, operand, quoted);
            if (included.empty()) {
                diagnostics_.error(line.location, "include file not found: " + operand);
                continue;
            }
            output << expand_includes(included, stack);
            continue;
        }
        output << line.text << '\n';
        line_locations_.push_back(line.location);
    }
    stack.pop_back();
    return output.str();
}

std::string Preprocessor::substitute(const Macro& macro,
                                     const std::vector<std::string>& arguments,
                                     std::unordered_set<std::string>& disabled,
                                     unsigned depth,
                                     std::optional<SourceLocation> condition,
                                     std::optional<SourceLocation> origin) const {
    std::unordered_map<std::string, std::string> replacements;
    for (std::size_t i = 0; i < macro.parameters.size(); ++i) {
        replacements[macro.parameters[i]] = i < arguments.size() ? arguments[i] : std::string{};
    }
    if (macro.variadic) {
        std::string variadic;
        for (std::size_t i = macro.parameters.size(); i < arguments.size(); ++i) {
            if (!variadic.empty()) variadic += ", ";
            variadic += arguments[i];
        }
        replacements["$::macro::args"] = std::move(variadic);
    }

    std::string output;
    for (std::size_t i = 0; i < macro.replacement.size();) {
        if (macro.replacement[i] == '#' &&
            (i + 1 >= macro.replacement.size() || macro.replacement[i + 1] != '#')) {
            ++i;
            while (i < macro.replacement.size() &&
                   std::isspace(static_cast<unsigned char>(macro.replacement[i])) != 0) ++i;
            const auto begin = i;
            while (i < macro.replacement.size() && identifier_continue(macro.replacement[i])) ++i;
            const auto found = replacements.find(macro.replacement.substr(begin, i - begin));
            if (found == replacements.end()) {
                output.push_back('#');
                output.append(macro.replacement, begin, i - begin);
                continue;
            }
            output.push_back('"');
            for (const char ch : trim(found->second)) {
                if (ch == '\\' || ch == '"') output.push_back('\\');
                output.push_back(ch);
            }
            output.push_back('"');
            continue;
        }
        if (identifier_start(macro.replacement[i])) {
            const auto begin = i++;
            while (i < macro.replacement.size() && identifier_continue(macro.replacement[i])) ++i;
            const auto name = macro.replacement.substr(begin, i - begin);
            const auto found = replacements.find(name);
            if (found != replacements.end()) {
                output += expand_text(found->second, disabled, depth + 1,
                                      condition, origin);
            } else {
                output += name;
            }
            continue;
        }
        if (macro.replacement[i] == '#' && i + 1 < macro.replacement.size() &&
            macro.replacement[i + 1] == '#') {
            while (!output.empty() && std::isspace(static_cast<unsigned char>(output.back())) != 0) output.pop_back();
            i += 2;
            while (i < macro.replacement.size() &&
                   std::isspace(static_cast<unsigned char>(macro.replacement[i])) != 0) ++i;
            continue;
        }
        output.push_back(macro.replacement[i++]);
    }
    return expand_text(output, disabled, depth + 1, condition, origin);
}

std::string Preprocessor::evaluate_query(
    std::string_view name, const std::vector<std::string>& arguments,
    std::optional<SourceLocation> origin) const {
    const auto normalized_argument = [&](std::size_t index) {
        auto result = trim(arguments[index]);
        if (const auto decoded = decode_string_literal(result)) result = *decoded;
        return result;
    };
    const auto* target = target_for_triple(options_.target);
    const auto* abi = target
        ? find_abi(*target, options_.abi, options_.target)
        : nullptr;
    const auto address_bits = abi && abi->address_bits != 0
                                  ? abi->address_bits
                                  : 64U;
    const auto feature_enabled = [&](std::string_view feature) {
        if (feature.empty() || feature == "base" ||
            (target && feature == target->architecture)) {
            return true;
        }
        bool enabled = false;
        for (const auto& selected : options_.target_features) {
            if (selected == "+" + std::string(feature)) enabled = true;
            else if (selected == "-" + std::string(feature)) enabled = false;
        }
        return enabled;
    };
    const auto instruction_enabled = [&](const InstructionEntry& instruction) {
        return feature_enabled(instruction.feature) &&
               std::all_of(
                   instruction.required_features.begin(),
                   instruction.required_features.end(), feature_enabled);
    };
    const auto scalar_bits = [address_bits](std::string_view type) {
        if (type == "bool" || type == "i8" || type == "u8") return 8u;
        if (type == "i16" || type == "u16") return 16u;
        if (type == "i32" || type == "u32" || type == "f32") return 32u;
        if (type == "i64" || type == "u64" || type == "f64") return 64u;
        if (type == "iptr" || type == "uptr" || type == "fptr") {
            return address_bits;
        }
        if (type == "i128" || type == "u128" || type == "f128") return 128u;
        if (type == "f80") return 80u;
        return 0u;
    };
    if (name == "$::has_patch_value") {
        if (arguments.size() != 1 || !target) return "0";
        const auto type = normalized_argument(0);
        const auto* materializer =
            find_patch_value_materializer(*target, type);
        return materializer && feature_enabled(materializer->feature) ? "1" : "0";
    }
    if (name == "$::has_patch_operand") {
        if (arguments.size() != 3 || !target) return "0";
        const auto instruction_name = normalized_argument(0);
        const auto operand_text = normalized_argument(1);
        const auto type = normalized_argument(2);
        std::size_t operand_index{};
        const auto parsed = std::from_chars(
            operand_text.data(), operand_text.data() + operand_text.size(),
            operand_index);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != operand_text.data() + operand_text.size()) {
            return "0";
        }
        const auto bits = scalar_bits(type);
        if (bits == 0) return "0";
        for (const auto* instruction :
             find_instruction_forms(*target, instruction_name)) {
            if (!instruction_enabled(*instruction) ||
                operand_index >= instruction->operands.size()) continue;
            const auto& operand = instruction->operands[operand_index];
            if (operand.patchable && operand.allow_immediate &&
                operand.immediate_bits == bits) return "1";
        }
        return "0";
    }
    if (arguments.size() != 1) return "0";
    if (name == "$::has_include") {
        const auto operand = trim(arguments.front());
        const bool quoted = operand.size() >= 2 && operand.front() == '"' && operand.back() == '"';
        const bool angled = operand.size() >= 2 && operand.front() == '<' && operand.back() == '>';
        if (!quoted && !angled) return "0";
        const auto written = quoted
            ? decode_string_literal(operand)
            : std::optional<std::string>(operand.substr(1, operand.size() - 2));
        if (!written || written->empty() || written->find('\0') != std::string::npos) return "0";
        const auto including = origin && origin->file
            ? origin->file->path : std::filesystem::path{};
        return find_include(including, *written, quoted).empty() ? "0" : "1";
    }
    auto argument = normalized_argument(0);
    const auto one_of = [&](std::initializer_list<std::string_view> entries) {
        return std::any_of(entries.begin(), entries.end(),
                           [&](std::string_view entry) { return argument == entry; });
    };
    if (name == "$::has_intrinsic") {
        return one_of({"$::expect", "$::assume", "$::unreachable", "$::trap",
                       "$::alignof", "$::static_assert", "$::patch",
                       "$::eval", "$::runtime", "$::quote", "$::unquote",
                       "$::meta::parse", "$::meta::concat",
                       "$::atomic_load", "$::atomic_store",
                       "$::atomic_exchange", "$::atomic_compare_exchange",
                       "$::atomic_fetch_add", "$::atomic_fetch_sub",
                       "$::atomic_fetch_and", "$::atomic_fetch_xor",
                       "$::atomic_fetch_or", "$::atomic_thread_fence",
                       "$::atomic_signal_fence",
                       "$::atomic_is_lock_free"}) ? "1" : "0";
    }
    if (name == "$::has_instruction") {
        if (!target) return "0";
        const auto forms = find_instruction_forms(*target, argument);
        return std::any_of(forms.begin(), forms.end(), [&](const auto* form) {
            return instruction_enabled(*form);
        }) ? "1" : "0";
    }
    if (name == "$::has_builtin") {
        return one_of({"$::expect", "$::assume", "$::unreachable", "$::trap",
                       "$::alignof", "$::static_assert", "$::patch",
                       "$::eval", "$::runtime", "$::quote", "$::unquote",
                       "$::meta::parse", "$::meta::concat",
                       "$::atomic_load", "$::atomic_store",
                       "$::atomic_exchange", "$::atomic_compare_exchange",
                       "$::atomic_fetch_add", "$::atomic_fetch_sub",
                       "$::atomic_fetch_and", "$::atomic_fetch_xor",
                       "$::atomic_fetch_or", "$::atomic_thread_fence",
                       "$::atomic_signal_fence", "$::atomic_is_lock_free",
                       "$::memory::relaxed", "$::memory::acquire",
                       "$::memory::release", "$::memory::acq_rel",
                       "$::memory::seq_cst"}) ||
                       ([&] {
                            if (!target) return false;
                            const auto forms =
                                find_instruction_forms(*target, argument);
                            return std::any_of(
                                forms.begin(), forms.end(),
                                [&](const auto* form) {
                                    return instruction_enabled(*form);
                                });
                       }()) ? "1" : "0";
    }
    if (name == "$::has_attribute") {
        return is_known_attribute(argument) ? "1" : "0";
    }
    if (name == "$::has_feature") {
        if (argument == "$::feature::address_spaces") {
            return target && std::any_of(
                       target->address_spaces.begin(),
                       target->address_spaces.end(),
                       [](const AddressSpaceEntry& entry) {
                           return entry.number != 0 && entry.native_lowering;
                       })
                       ? "1" : "0";
        }
        if (one_of({"$::feature::runtime_free_intrinsics",
                    "$::feature::control_intrinsics",
                    "$::feature::evaluation",
                    "$::feature::automatic_evaluation",
                    "$::feature::generics",
                    "$::feature::procedural_macros",
                    "$::feature::patchable_values",
                    "$::feature::patchable_operands",
                    "$::feature::raw_inline",
                    "$::feature::contextual_attributes",
                    "$::feature::external_models",
                    "$::feature::operator_binding"})) return "1";
        if (target && target->architecture == "x86-64" &&
            one_of({"$::feature::integer128",
                    "$::feature::binary128_storage",
                    "$::feature::binary128_arithmetic",
                    "$::feature::fixed_vectors",
                    "$::feature::atomics",
                    "$::feature::variadics",
                    "$::feature::thread_local"})) {
            return "1";
        }
        if (target && target->architecture == "mips" &&
            argument == "$::feature::atomics") {
            return resolved_bool(options_, "m.llsc") ? "1" : "0";
        }
        constexpr std::string_view prefix = "$::feature::";
        if (argument.starts_with(prefix)) {
            return feature_enabled(argument.substr(prefix.size())) ? "1" : "0";
        }
        return "0";
    }
    if (name == "$::has_abi") {
        return target && find_abi(*target, argument, options_.target) ? "1" : "0";
    }
    if (name == "$::has_mangling") {
        return model_registry().find_mangling(argument) ? "1" : "0";
    }
    if (name == "$::has_profile") {
        return model_registry().find_profile(argument) ? "1" : "0";
    }
    return "0";
}

std::string Preprocessor::expand_text(std::string_view text,
                                      std::unordered_set<std::string>& disabled,
                                      unsigned depth,
                                      std::optional<SourceLocation> condition,
                                      std::optional<SourceLocation> origin) const {
    if (depth > 100) return std::string(text);
    std::string output;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '"' || text[i] == '\'') {
            const char quote = text[i];
            do {
                const char ch = text[i++];
                output.push_back(ch);
                if (ch == '\\' && i < text.size()) output.push_back(text[i++]);
            } while (i < text.size() && text[i] != quote);
            if (i < text.size()) output.push_back(text[i++]);
            continue;
        }
        if (!identifier_start(text[i])) {
            output.push_back(text[i++]);
            continue;
        }
        const auto begin = i++;
        while (i < text.size() && identifier_continue(text[i])) ++i;
        const std::string name(text.substr(begin, i - begin));
        if (condition && name == "defined") {
            const auto skip_space = [&] {
                while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            };
            skip_space();
            const bool parenthesized = i < text.size() && text[i] == '(';
            if (parenthesized) { ++i; skip_space(); }
            const auto operand_begin = i;
            while (i < text.size() && identifier_continue(text[i])) ++i;
            const auto operand = text.substr(operand_begin, i - operand_begin);
            skip_space();
            if (!valid_macro_name(operand) ||
                (parenthesized && (i == text.size() || text[i] != ')'))) {
                diagnostics_.error(*condition, "defined requires a macro name, optionally parenthesized");
                return "0";
            }
            if (parenthesized) ++i;
            output += macros_.contains(std::string(operand)) ? "1 " : "0 ";
            continue;
        }
        if (origin && origin->valid() && name == "$::source::file") {
            std::ostringstream spelling;
            spelling << std::quoted(origin->file->path.generic_string());
            output += spelling.str();
            continue;
        }
        if (origin && origin->valid() && name == "$::source::line") {
            output += std::to_string(origin->line);
            continue;
        }
        const auto found = macros_.find(name);
        if (found == macros_.end() || disabled.contains(name)) {
            output += name;
            continue;
        }
        const auto& macro = found->second;
        disabled.insert(name);
        if (!macro.function_like) {
            output += expand_text(macro.replacement, disabled, depth + 1,
                                  condition, origin);
            disabled.erase(name);
            continue;
        }
        auto open = i;
        while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open])) != 0) ++open;
        if (open >= text.size() || text[open] != '(') {
            disabled.erase(name);
            output += name;
            continue;
        }
        std::vector<std::string> arguments;
        std::string argument;
        unsigned nesting = 1;
        char quote = '\0';
        i = open + 1;
        while (i < text.size() && nesting != 0) {
            const char ch = text[i++];
            if (quote != '\0') {
                argument.push_back(ch);
                if (ch == '\\' && i < text.size()) argument.push_back(text[i++]);
                else if (ch == quote) quote = '\0';
                continue;
            }
            if (ch == '"' || ch == '\'') { quote = ch; argument.push_back(ch); continue; }
            if (ch == '(') { ++nesting; argument.push_back(ch); continue; }
            if (ch == ')') {
                if (--nesting == 0) {
                    if (!argument.empty() || !arguments.empty() || !macro.parameters.empty()) {
                        arguments.push_back(trim(argument));
                    }
                    break;
                }
                argument.push_back(ch);
                continue;
            }
            if (ch == ',' && nesting == 1) {
                arguments.push_back(trim(argument));
                argument.clear();
                continue;
            }
            argument.push_back(ch);
        }
        if (nesting != 0) {
            output += name;
            output.append(text.substr(open, i - open));
            disabled.erase(name);
            continue;
        }
        if (name.starts_with("$::has_")) {
            for (auto& query_argument : arguments)
                query_argument = expand_text(query_argument, disabled,
                                             depth + 1, condition, origin);
            output += evaluate_query(name, arguments, origin);
        } else {
            output += substitute(macro, arguments, disabled, depth + 1,
                                 condition, origin);
        }
        disabled.erase(name);
    }
    return output;
}

std::string Preprocessor::expand_macros(std::string_view source, const SourceFile* file) {
    std::istringstream input{std::string(source)};
    std::ostringstream output;
    struct ConditionalGroup {
        SourceLocation location;
        bool parent_active{};
        bool branch_taken{};
        bool active{};
        bool else_seen{};
    };
    std::vector<ConditionalGroup> conditions;
    const auto* target = target_for_triple(options_.target);
    const auto* abi = target ? find_abi(*target, options_.abi, options_.target) : nullptr;
    const auto address_bits = abi && abi->address_bits ? abi->address_bits : 64U;
    const auto evaluate = [&](std::string_view expression, SourceLocation location) {
        std::unordered_set<std::string> disabled;
        const auto errors = diagnostics_.errors();
        const auto expanded = expand_text(expression, disabled, 0, location,
                                          location);
        return diagnostics_.errors() == errors &&
            evaluate_preprocessing_condition(expanded, location, diagnostics_, address_bits);
    };
    std::string line;
    unsigned line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto stripped = trim(line);
        const auto location = line_number <= line_locations_.size()
            ? line_locations_[line_number - 1] : SourceLocation{file, 0, line_number, 1};
        const bool active = conditions.empty() || conditions.back().active;
        if (!stripped.starts_with('#')) {
            if (active) {
                std::unordered_set<std::string> disabled;
                output << expand_text(line, disabled, 0, {}, location) << '\n';
                output_line_locations_.push_back(location);
            }
            continue;
        }
        const auto command = directive(stripped);
        if (command.name == "if" || command.name == "ifdef" || command.name == "ifndef") {
            bool selected = false;
            if (active) {
                if (command.name == "if") selected = evaluate(command.operand, location);
                else if (!valid_macro_name(command.operand)) {
                    diagnostics_.error(location, "#" + command.name + " requires exactly one macro name");
                } else {
                    selected = macros_.contains(command.operand);
                    if (command.name == "ifndef") selected = !selected;
                }
            }
            conditions.push_back({location, active, selected, active && selected, false});
            continue;
        }
        if (command.name == "elif" || command.name == "else" || command.name == "endif") {
            if (conditions.empty()) {
                diagnostics_.error(location, "unmatched #" + command.name);
                continue;
            }
            auto& group = conditions.back();
            if (command.name != "elif" && !command.operand.empty())
                diagnostics_.error(location, "unexpected tokens after #" + command.name);
            if (command.name == "endif") {
                conditions.pop_back();
                continue;
            }
            if (group.else_seen) {
                diagnostics_.error(location, "#" + command.name + " after #else");
                group.active = false;
                continue;
            }
            if (command.name == "else") {
                group.else_seen = true;
                group.active = group.parent_active && !group.branch_taken;
            } else {
                group.active = group.parent_active && !group.branch_taken &&
                    evaluate(command.operand, location);
            }
            group.branch_taken = group.branch_taken || group.active;
            continue;
        }
        if (!active) continue;
        if (command.name == "define") {
            const auto& definition = command.operand;
            std::size_t cursor = 0;
            while (cursor < definition.size() && identifier_continue(definition[cursor])) ++cursor;
            const auto name = definition.substr(0, cursor);
            if (!valid_macro_name(name) || name.starts_with("$::")) {
                diagnostics_.error(location, "#define requires a non-builtin macro name");
                continue;
            }
            Macro macro;
            if (cursor < definition.size() && definition[cursor] == '(') {
                macro.function_like = true;
                const auto close = definition.find(')', cursor + 1);
                if (close == std::string::npos) {
                    diagnostics_.error(location, "unterminated macro parameter list");
                    continue;
                }
                auto parameters = std::string_view(definition).substr(cursor + 1, close - cursor - 1);
                while (!parameters.empty()) {
                    const auto comma = parameters.find(',');
                    auto parameter = trim(parameters.substr(0, comma));
                    if (parameter == "...") macro.variadic = true;
                    else if (!parameter.empty()) macro.parameters.push_back(std::move(parameter));
                    if (comma == std::string_view::npos) break;
                    parameters.remove_prefix(comma + 1);
                }
                cursor = close + 1;
            }
            macro.replacement = trim(std::string_view(definition).substr(cursor));
            macros_[name] = std::move(macro);
            continue;
        }
        if (command.name == "undef") {
            if (!valid_macro_name(command.operand) || command.operand.starts_with("$::"))
                diagnostics_.error(location, "#undef requires a non-builtin macro name");
            else macros_.erase(command.operand);
            continue;
        }
        if (command.name == "error") {
            diagnostics_.error(location, "#error " + command.operand);
            continue;
        }
        if (command.name == "warning") {
            diagnostics_.warning(location, "#warning " + command.operand);
            continue;
        }
        if (command.name == "require") {
            std::unordered_set<std::string> disabled;
            const auto errors = diagnostics_.errors();
            const auto expanded = expand_text(command.operand, disabled, 0,
                                              location, location);
            if (diagnostics_.errors() != errors) continue;
            const auto separator = top_level_comma(expanded);
            const auto condition = trim(std::string_view(expanded).substr(
                0, separator.value_or(expanded.size())));
            std::optional<std::string> reason;
            if (separator) {
                reason = require_reason(
                    std::string_view(expanded).substr(*separator + 1));
                if (!reason) {
                    diagnostics_.error(
                        location, "#require reason must be one string literal");
                    continue;
                }
            }
            const auto expression_errors = diagnostics_.errors();
            const bool satisfied = evaluate_preprocessing_condition(
                condition, location, diagnostics_, address_bits);
            if (diagnostics_.errors() == expression_errors && !satisfied) {
                diagnostics_.error(
                    location, "#require condition is false" +
                                  (reason ? ": " + *reason : std::string{}));
            }
            continue;
        }
        diagnostics_.error(location, "unsupported preprocessing directive");
    }
    for (const auto& group : conditions)
        diagnostics_.error(group.location, "unterminated conditional directive");
    return output.str();
}

std::string Preprocessor::process(const std::filesystem::path& input) {
    line_locations_.clear();
    output_line_locations_.clear();
    dependencies_.clear();
    dependency_identities_.clear();
    std::vector<std::filesystem::path> stack;
    auto included = expand_includes(input, stack);
    if (diagnostics_.errors() != 0) return {};
    // Namespace canonicalization is represented structurally by the parser in
    // this first implementation stage; macro expansion still observes the
    // required include -> namespace -> macro phase boundary.
    return expand_macros(included, nullptr);
}

} // namespace cross
