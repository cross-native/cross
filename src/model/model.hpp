// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/target.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cross {

class Diagnostics;
struct CompilerOptions;

enum class ManglingExpressionKind {
    Literal,
    IntegerLiteral,
    BooleanLiteral,
    Name,
    Entity,
    Kind,
    Result,
    Variadic,
    ParameterCount,
    Text,
    Mode,
    Index,
    Count,
    SubstitutionIndex,
    Variable,
    HelperCall,
    Concat,
    Decimal,
    Hex,
    Radix,
    Bytes,
    Length,
    Path,
    Arguments,
    Parameters,
    Select,
    Equal,
    Not,
    And,
    Or,
    Add,
    Subtract,
    Slice,
    Replace,
    Lookup,
    Lower,
    Upper,
    StartsWith,
    EndsWith,
    Contains,
    Substitute,
};

struct ManglingExpression {
    ManglingExpressionKind kind{ManglingExpressionKind::Literal};
    std::string literal;
    std::uint64_t integer{};
    bool boolean{};
    std::vector<ManglingExpression> operands;
    unsigned line{};
};

enum class ManglingValueType { Text, Integer, Boolean };

struct ManglingHelperParameter {
    std::string name;
    ManglingValueType type{ManglingValueType::Text};
};

struct ManglingHelper {
    std::string name;
    ManglingValueType result_type{ManglingValueType::Text};
    std::vector<ManglingHelperParameter> parameters;
    ManglingExpression expression;
    unsigned line{};
};

struct ManglingEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    ManglingExpression entity;
    ManglingExpression label;
    ManglingExpression generic;
    std::vector<ManglingHelper> helpers;
};

struct ManglingArgument {
    enum class Kind { Type, Value } kind{Kind::Type};
    std::string spelling;
};

struct ManglingParameter {
    std::string spelling;
    std::string mode;
};

struct ManglingEntityDescriptor {
    std::string_view qualified_name;
    std::string_view kind;
    std::string_view result;
    std::span<const ManglingParameter> parameters;
    bool variadic{};
};

struct ProfileEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    std::vector<std::string> default_for;
    std::optional<std::string> target;
    std::optional<std::string> abi;
    std::optional<std::string> mangling;
    std::optional<std::string> optimization;
    std::vector<OptionAssignment> options;
};

struct OptimizationEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    std::optional<std::string> inherits;
    std::vector<std::string> targets;
    std::vector<OptionAssignment> options;
};

struct ShippedModelSource {
    std::string_view name;
    std::string_view text;
};

std::span<const ShippedModelSource> shipped_model_sources();

class ModelRegistry {
public:
    ModelRegistry();

    bool load_file(const std::filesystem::path& path, Diagnostics& diagnostics);

    [[nodiscard]] const AbiEntry* find_abi(
        std::string_view architecture, std::string_view name,
        std::string_view triple) const;
    [[nodiscard]] const AbiEntry* find_abi(AbiId id) const;
    [[nodiscard]] std::string_view default_abi(
        std::string_view architecture, std::string_view triple) const;
    [[nodiscard]] const ManglingEntry* find_mangling(
        std::string_view name) const;
    [[nodiscard]] const ProfileEntry* find_profile(
        std::string_view name) const;
    [[nodiscard]] const OptimizationEntry* find_optimization(
        std::string_view name) const;
    // The profiles whose default_for patterns match `triple` most
    // specifically; more than one is an ambiguous default.
    [[nodiscard]] std::vector<const ProfileEntry*> default_profiles(
        std::string_view triple) const;
    [[nodiscard]] const ProfileEntry* default_profile(
        std::string_view triple) const;

    [[nodiscard]] const std::vector<AbiEntry>& abis() const { return abis_; }
    [[nodiscard]] const std::vector<ManglingEntry>& manglings() const {
        return manglings_;
    }
    [[nodiscard]] const std::vector<ProfileEntry>& profiles() const {
        return profiles_;
    }
    [[nodiscard]] const std::vector<OptimizationEntry>& optimizations() const {
        return optimizations_;
    }
    [[nodiscard]] const std::vector<std::string>& origins() const {
        return origins_;
    }

private:
    bool load_text(std::string_view text, std::string origin,
                   Diagnostics& diagnostics);

    std::vector<AbiEntry> abis_;
    std::vector<ManglingEntry> manglings_;
    std::vector<ProfileEntry> profiles_;
    std::vector<OptimizationEntry> optimizations_;
    std::vector<std::string> origins_;
    std::unordered_set<std::string> loaded_files_;
};

ModelRegistry& model_registry();

// Loads explicitly requested model sources and applies one profile. Command-line
// selections always override profile defaults, independent of argument order.
bool configure_models(CompilerOptions& options, Diagnostics& diagnostics);

std::string encode_model_link_name(std::string_view qualified_name, bool label,
                                   std::string_view mangling_name);
std::string encode_model_link_name(
    const ManglingEntityDescriptor& entity, bool label,
    std::string_view mangling_name);
std::string encode_model_generic_link_name(
    std::string_view qualified_name,
    std::span<const ManglingArgument> arguments,
    std::string_view mangling_name);
std::string encode_model_generic_link_name(
    const ManglingEntityDescriptor& entity,
    std::span<const ManglingArgument> arguments,
    std::string_view mangling_name);

} // namespace cross
