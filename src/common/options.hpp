// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cross {

enum class EmitKind {
    Link,
    Assembly,
    Object,
    LlvmTextDebug,
    GimpleTextDebug,
    GimpleRtlTextDebug,
    Preprocess,
};

enum class OptimizationGoal { Debug, Speed, Size, MinimumSize };
enum class CodeModel { Small, Kernel, Medium, Large };
enum class FpContractMode { Off, On, Fast };

enum class OptionValueKind { Boolean, Unsigned, Enumeration, Text };
enum class OptionCategory { Optimization, CodeGeneration, Semantic, Target };
enum class OptionImplementation { Implemented, Partial };
enum class OptionOrigin { Default, Preset, Profile, CommandLine };

using OptionValue = std::variant<bool, std::uint64_t, std::string>;

struct OptionDefinition {
    std::string name;
    std::vector<std::string> aliases;
    OptionValueKind kind{OptionValueKind::Boolean};
    OptionValue default_value{false};
    std::vector<std::string> allowed_values;
    std::uint64_t minimum{};
    std::uint64_t maximum{~std::uint64_t{}};
    OptionCategory category{OptionCategory::Optimization};
    bool presettable{};
    OptionImplementation implementation{OptionImplementation::Implemented};
    std::string description;
};

// Model assignments are already typed. Command-line value assignments retain
// text until the selected target registry supplies their type.
struct OptionAssignment {
    std::string name;
    OptionValue value;
    std::string source;
};

struct ResolvedOption {
    OptionValue value;
    OptionOrigin origin{OptionOrigin::Default};
    std::string source{"default"};
};

struct CompilerOptions {
    EmitKind emit{EmitKind::Link};
    std::string optimization{"O0"};
    std::string target{"x86_64-w64-windows-gnu"};
    std::string cpu{"generic"};
    std::string tune{"generic"};
    std::string abi{"default"};
    std::string mangling{"default"};
    std::string profile{"default"};
    std::vector<std::string> target_features;
    std::vector<OptionAssignment> command_options;
    std::unordered_map<std::string, ResolvedOption> resolved_options;
    std::vector<std::filesystem::path> inputs;
    std::optional<std::filesystem::path> output;
    std::vector<std::filesystem::path> include_paths;
    std::vector<std::filesystem::path> model_paths;
    std::vector<std::filesystem::path> model_files;
    std::vector<std::string> macro_definitions;
    std::vector<std::string> macro_undefinitions;

    // Typed hot-path values populated from resolved_options. The registry and
    // its origin metadata remain authoritative; these fields keep lowering
    // independent from command/model spellings.
    OptimizationGoal optimize_for{OptimizationGoal::Debug};
    unsigned ccp_rounds{1};
    unsigned if_conversion_limit{6};
    unsigned if_conversion_memory_limit{12};
    unsigned unroll_factor{2};
    unsigned vector_interleave{2};
    unsigned risc_cisc_balance{50};
    unsigned function_alignment{};
    unsigned loop_alignment{};
    bool function_sections{};
    bool data_sections{};
    bool position_independent{};
    bool pie{};
    bool use_plt{true};
    bool semantic_interposition{true};
    bool direct_external_data{};
    bool unwind_tables{true};
    bool asynchronous_unwind_tables{true};
    CodeModel code_model{CodeModel::Small};
    bool fast_math{};
    FpContractMode fp_contract{FpContractMode::Off};
    bool finite_math_only{};
    bool signed_zeros{true};
    bool evaluate_calls{true};
    bool omit_frame_pointer{};
    bool tree_ccp{};
    bool tree_bit_ccp{};
    bool tree_copy_prop{};
    bool tree_dce{};
    bool tree_dse{};
    bool tree_fre{};
    bool tree_tail_merge{};
    bool thread_jumps{};
    bool tree_cfg_cleanup{};
    bool ivopts{};
    bool move_loop_invariants{};
    bool unroll_loops{};
    bool tree_reassoc{};
    bool tree_slsr{};
    bool tree_loop_rotate{};
    bool tree_loop_vectorize{};
    bool tree_early_exit_vectorize{};
    bool tree_slp_vectorize{};
    bool if_conversion{};
    bool expensive_optimizations{};
    bool machine_combine{};
    bool vector_combine{};
    bool machine_cse{};
    bool machine_load_cse{};
    bool machine_dce{};
    bool compare_branch_fusion{};
    bool compare_select_fusion{};
    bool jump_tables{};
    bool combine_addresses{};
    bool fold_memory_operands{};
    bool schedule_insns{};
    bool schedule_insns2{};
    bool reorder_blocks{};
    bool register_allocation{};
    bool rematerialize{};
    bool optimize_sibling_calls{};
    bool cprop_registers{};
    bool peephole2{};
    bool inline_functions{};
    bool ipa_ra{};
    bool ipa_pure_const{};
    bool private_abi{};
    bool ipa_cp_clone{};
    unsigned inline_unit_limit{96};

    bool verbose{};
    bool save_temps{};
    bool show_help{};
    bool show_version{};
    bool print_targets{};
    bool print_abis{};
    bool print_models{};
    bool print_profiles{};
    bool print_manglings{};
    bool print_optimizations{};
    bool print_options{};
    std::string print_options_category{"all"};
    bool print_attributes{};
    bool print_registers{};
    bool print_keywords{};
    bool print_builtins{};
    bool print_instructions{};
    bool print_features{};

    // Profiles supply defaults only. These markers preserve explicit routing
    // choices independently of argument order. Registered -f/-m settings are
    // kept in command_options and are always applied last.
    bool target_explicit{};
    bool abi_explicit{};
    bool mangling_explicit{};
    bool profile_explicit{};
    bool optimization_explicit{};
};

bool parse_cc_options(int argc, char** argv, CompilerOptions& options,
                      Diagnostics& diagnostics);
bool parse_cpp_options(int argc, char** argv, CompilerOptions& options,
                       Diagnostics& diagnostics);

std::span<const OptionDefinition> common_option_definitions();
bool resolve_registered_options(
    CompilerOptions& options,
    std::span<const OptionDefinition> target_definitions,
    std::span<const OptionAssignment> preset_options,
    std::span<const OptionAssignment> profile_options,
    Diagnostics& diagnostics);

const ResolvedOption* find_resolved_option(const CompilerOptions& options,
                                           std::string_view name);
bool resolved_bool(const CompilerOptions& options, std::string_view name,
                   bool fallback = false);
std::uint64_t resolved_unsigned(const CompilerOptions& options,
                                std::string_view name,
                                std::uint64_t fallback = 0);
std::string_view resolved_text(const CompilerOptions& options,
                               std::string_view name,
                               std::string_view fallback = {});
std::string_view option_origin_name(OptionOrigin origin);
std::string option_value_text(const OptionValue& value);
std::string option_type_text(const OptionDefinition& definition);

void print_cc_help();
void print_cpp_help();
void print_version();

} // namespace cross
