// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/target.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
    std::optional<std::string> debug;
    // The init_table entries of [[constructor]] and [[destructor]].
    std::optional<std::string> constructors;
    std::optional<std::string> destructors;
    std::vector<OptionAssignment> options;
    floating::Environment floating_environment;
};

struct OptimizationEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    std::vector<std::string> inherits;  // the bases, in order
    std::vector<std::string> targets;
    std::vector<OptionAssignment> options;
};

// The debugging information -g emits; `dwarf` is the only format.
struct DebugEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    unsigned version{5};
    // Call-frame information goes to the loaded .eh_frame, not .debug_frame.
    bool eh_frame{};
    bool lines{true};
    bool frames{true};
    bool variables{true};
    bool types{true};
};

// One `instruction` block of a cpu entry. `instruction` is valid once the
// loaded models are complete.
struct CpuInstructionEntry {
    std::string name;
    unsigned line{};
    InstructionCostId instruction;
    std::optional<unsigned> latency;
    std::optional<unsigned> issue;
    std::optional<bool> available;
};

// An operand of an `instruction` entry: a named operand of a target
// operand type, or a literal register.
struct ModelOperand {
    std::string spelling;  // as the entry lists it
    std::string name;      // empty for a literal register
    std::string type_name;
    OperandType type;
    std::optional<std::int64_t> default_value;
    std::optional<unsigned> default_operand;
    unsigned literal_number{};
};

// A term of an encoding field or a constraint: an operand's value, or the
// base (part 1) or offset (part 2) of a memory operand.
struct ModelTerm {
    unsigned operand{};
    unsigned part{};
    bool negative{};
};

// A field of an instruction word: the sum of its terms and constant, or a
// register's number, from bit `low` of that value up, at bit `shift`.
struct ModelField {
    unsigned shift{};
    unsigned width{};
    std::vector<ModelTerm> terms;
    std::int64_t constant{};
    unsigned low{};
    std::optional<unsigned> register_number;
    std::string_view register_class{};
};

// An item of `reads` or `writes`.
struct ModelEffect {
    enum class Kind : std::uint8_t { Operand, Register, Resource };
    Kind kind{};
    unsigned operand{};
    std::string name;  // the register or resource
};

// A line of `expands` or `otherwise`: an instruction and its operands,
// each an operand of the macro entry, its negation, or a register.
struct ModelExpansion {
    struct Operand {
        enum class Kind : std::uint8_t { Operand, Negated, Register };
        Kind kind{};
        unsigned operand{};
        std::string register_name;
        unsigned register_number{};
    };
    std::string mnemonic;
    std::vector<Operand> operands;
    // The entry it emits, once the table is complete.
    std::size_t entry{};
};

struct ModelConstraint {
    using Comparison = InstructionConstraint::Comparison;
    std::vector<ModelTerm> terms;
    std::int64_t constant{};
    Comparison comparison{};
    std::int64_t bound{};
    std::string text;

    // Whether `sum`, of the terms and the constant, satisfies the constraint.
    [[nodiscard]] bool holds(std::int64_t sum) const {
        return InstructionConstraint{{}, constant, comparison, bound}.holds(sum);
    }
};

// One `instruction` entry: a form of a machine instruction.
struct ModelInstruction {
    std::string name;
    std::string mnemonic;
    std::string architecture;
    std::string origin;  // the model file
    unsigned line{};
    bool shipped{};
    std::vector<ModelOperand> operands;
    std::uint64_t fixed_bits{};
    std::vector<ModelField> fields;
    bool encoded{};
    std::vector<std::string> features;
    std::vector<std::string> excludes;
    std::vector<ModelEffect> reads;
    std::vector<ModelEffect> writes;
    std::vector<std::string> hazards;
    InstructionControlEffect control{InstructionControlEffect::None};
    InstructionBranchCondition branch_condition{InstructionBranchCondition::None};
    bool delay_slot{};
    bool likely{};
    bool privileged{};
    bool volatile_effect{};
    // HazardFact::resource refers to `hazard_resources`.
    std::vector<std::string> hazard_resources;
    std::vector<HazardFact> hazard_uses;
    std::vector<HazardFact> hazard_writes;
    std::optional<unsigned> latency;
    std::optional<unsigned> issue;
    // Register operands that the form writes only in part or only on a
    // condition, keeping the rest of their value.
    std::vector<unsigned> keeps;
    // The name of its built-in, "$::_NAME": `builtin`, or once the table is
    // complete the mnemonic's; empty with `builtin = false`.
    std::string builtin;
    bool no_builtin{};
    // Why the form gives no built-in, once the table is complete.
    std::string unexposed;
    std::string alias;
    std::size_t alias_entry{};
    std::string macro;
    std::vector<ModelExpansion> expands;
    std::vector<ModelExpansion> otherwise;
    std::vector<ModelConstraint> constraints;
    // The lines of the entry's properties, for checks of the whole table.
    std::vector<std::pair<std::string, unsigned>> property_lines;

    // "file:line" of `property`, or of the entry.
    [[nodiscard]] std::string position(std::string_view property = {}) const;
    // The entry that an alias re-spells, or the entry itself.
    [[nodiscard]] bool is_alias() const { return !alias.empty(); }
};

// The instruction table of an architecture, from its `instruction`
// entries, and what it gives the target.
struct InstructionTable {
    std::string architecture;
    std::vector<ModelInstruction> entries;
    // The forms of each mnemonic, in table order.
    std::unordered_map<std::string_view, std::vector<std::size_t>> forms;
    // The target's instruction costs, registry forms, and their costs.
    std::vector<InstructionCostEntry> costs;
    std::vector<InstructionCostId> cost_listing;
    std::vector<InstructionEntry> builtins;
    std::vector<InstructionCostId> builtin_costs;
    // The entry of each registry form: the alias that spells it, or the
    // entry that encodes it.
    std::vector<std::size_t> builtin_entries;
    // The built-in names of entries that no form gives.
    std::vector<UnexposedInstruction> unexposed;
};

struct CpuEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    std::string architecture;
    std::vector<std::string> inherits;  // the bases, in order
    std::optional<std::vector<std::string>> features;
    std::optional<unsigned> issue_width;
    // Bytes, a power of two.
    std::optional<unsigned> function_alignment;
    std::vector<CpuInstructionEntry> instructions;
};

// Data that code reaches through a register holding a fixed base address.
struct DataBaseEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    std::string base;    // a register name
    std::string base_source;  // "file:line" of the base property
    std::vector<std::string> sections;
    unsigned max_size{};
    // The kinds of objects max_size places.
    bool writable{true};
    bool readonly{};
    bool literal{};
};

// A table of functions that a program's runtime calls, such as ELF's
// .init_array.
struct InitTableEntry {
    std::string canonical_name;
    std::string source;  // "file:line" of the declaration
    std::string section;
    // The start of the section names of prioritized entries.
    std::optional<std::string> priority_section;
    bool invert_priority{};
    // The ABI of the calls; unset, the compilation's.
    std::optional<std::string> abi;
};

// A cpu entry with its bases applied.
struct ResolvedCpu {
    std::vector<std::string> features;
    unsigned issue_width{1};
    // Unset: the target's choice.
    std::optional<unsigned> function_alignment;
    // By InstructionCostId; instructions no entry describes keep the
    // target's defaults.
    std::vector<InstructionCost> instructions;
};

// A shipped model file's text, in pieces short enough for string literals.
struct ShippedModelSource {
    std::string_view name;
    std::span<const std::string_view> text;
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
    [[nodiscard]] const DebugEntry* find_debug(std::string_view name) const;
    [[nodiscard]] const CpuEntry* find_cpu(std::string_view architecture,
                                           std::string_view name) const;
    [[nodiscard]] const DataBaseEntry* find_data_base(
        std::string_view name) const;
    [[nodiscard]] const InitTableEntry* find_init_table(
        std::string_view name) const;
    [[nodiscard]] ResolvedCpu resolve_cpu(const TargetInfo& target,
                                          const CpuEntry& entry) const;
    // The instruction table of an architecture whose instructions come from
    // models, or none.
    [[nodiscard]] const InstructionTable* instruction_table(
        std::string_view architecture) const;
    // The instructions that cpu entries of the target describe.
    [[nodiscard]] std::span<const InstructionCostEntry> instruction_costs(
        const TargetInfo& target) const;
    // Checks the instruction entries of every loaded file as a whole and
    // builds the instruction tables, then resolves the instructions that
    // cpu entries describe; returns false after a diagnostic.
    bool complete_instructions(Diagnostics& diagnostics);
    // Gives each target the instruction table that the models built.
    void install_instruction_tables() const;
    // The f.* and m.* settings of a preset with its bases applied, by name.
    [[nodiscard]] std::vector<OptionAssignment> resolve_optimization(
        const OptimizationEntry& entry) const;
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
    [[nodiscard]] const std::vector<DebugEntry>& debugs() const {
        return debugs_;
    }
    [[nodiscard]] const std::vector<CpuEntry>& cpus() const { return cpus_; }
    [[nodiscard]] const std::vector<DataBaseEntry>& data_bases() const {
        return data_bases_;
    }
    [[nodiscard]] const std::vector<InitTableEntry>& init_tables() const {
        return init_tables_;
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
    std::vector<DebugEntry> debugs_;
    std::vector<CpuEntry> cpus_;
    std::vector<DataBaseEntry> data_bases_;
    std::vector<InitTableEntry> init_tables_;
    std::vector<InstructionTable> instruction_tables_;
    // Instruction entries arrived since the tables were completed.
    bool instructions_pending_{};
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
