// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver/driver.hpp"

#if CROSS_ENABLE_LLVM_TEXT
#include "backend/llvm/llvm_ir.hpp"
#endif
#if CROSS_ENABLE_GCC_GIMPLE_TEXT
#include "backend/gcc/gimple_ir.hpp"
#endif
#include "backend/native/assembler.hpp"
#include "backend/native/data_emitter.hpp"
#include "backend/native/debug_info.hpp"
#include "common/options.hpp"
#include "common/relocation_addend.hpp"
#include "common/source.hpp"
#include "frontend/lexer.hpp"
#include "frontend/builtin_registry.hpp"
#include "frontend/embed.hpp"
#include "frontend/parser.hpp"
#include "frontend/preprocessor.hpp"
#include "frontend/procedural.hpp"
#include "frontend/semantic.hpp"
#include "frontend/syntax.hpp"
#include "middle/hir.hpp"
#include "middle/initializer.hpp"
#include "middle/codegen_module.hpp"
#include "middle/data_ir.hpp"
#include "middle/mir.hpp"
#include "model/model.hpp"
#include "target/backend.hpp"
#include "target/target.hpp"
#include "target/instruction_constraints.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cross {
namespace {

TypePtr patch_address_type(PatchAddressRepresentation representation, unsigned address_bits) {
    if (!patch_address_storage_bytes(representation, address_bits)) return {};
    switch (representation) {
    case PatchAddressRepresentation::Unavailable: return {};
    case PatchAddressRepresentation::FlatUptr: return builtin_type(BuiltinType::Uptr);
    }
    return {};
}

template <typename LayoutFor>
void install_evaluation_layout_queries(Program& current, const TargetInfo* target,
                                       const Subtarget& subtarget, LayoutFor layout_for) {
    current.evaluation_size_of = [layout_for, target](const TypePtr& type)
        -> ContinuationTask<std::optional<std::uint64_t>> {
        const auto layout = co_await layout_for(type);
        if (!layout) co_return std::nullopt;
        co_return hir::layout_size(*layout, layout->intern_type(type), *target);
    };
    current.evaluation_align_of = [layout_for, target](const TypePtr& type)
        -> ContinuationTask<std::optional<std::uint64_t>> {
        const auto layout = co_await layout_for(type, EvaluationLayoutKind::Alignment);
        if (!layout) co_return std::nullopt;
        co_return hir::layout_alignment(*layout, layout->intern_type(type), *target);
    };
    current.evaluation_atomic_is_lock_free = [layout_for, target, &subtarget](const TypePtr& type)
        -> ContinuationTask<std::optional<bool>> {
        const auto layout = co_await layout_for(type);
        if (!layout) co_return {};
        co_return hir::lock_free_atomic_type(*layout, layout->intern_type(type), *target, subtarget);
    };
    current.evaluation_relocation_addend = [](const RelocationAddend& value) {
        // Both source-entity and Data IR relocation records currently carry
        // signed 64-bit displacements, independently of the selected ABI width.
        return relocation_addend_i64(value).has_value();
    };
    current.evaluation_address_space_type_error = [target](std::uint32_t number) {
        return address_space_type_error(*target, number);
    };
    current.evaluation_patch_value_capabilities = [target, &subtarget, &current](const TypePtr& type)
        -> std::optional<EvaluationPatchValueCapabilities> {
        // Enums use their underlying scalar materializer. Top-level source
        // qualifiers do not create a different machine representation.
        const auto* entry = type && type->kind == Type::Kind::Builtin
            ? find_patch_value_materializer(*target, type_name(builtin_type(type->builtin))) : nullptr;
        if (!entry || !subtarget.supports_registry_feature(entry->feature)) return {};
        return EvaluationPatchValueCapabilities{patch_address_type(entry->patch_address, current.address_bits),
                                                entry->supports_symbol_relocation};
    };
    current.evaluation_patch_operand_capabilities = [target, &subtarget, &current](std::string_view instruction,
        std::size_t index, std::size_t count, const TypePtr& type) {
        std::vector<EvaluationPatchValueCapabilities> candidates;
        if (!type || type->kind != Type::Kind::Builtin || type->nominal_key() != NominalTypeKey{})
            return candidates;
        const auto name = type_name(builtin_type(type->builtin));
        const auto bits = type->builtin == BuiltinType::Iptr || type->builtin == BuiltinType::Uptr
            ? current.address_bits : type_bits(type);
        for (const auto* form : find_instruction_forms(*target, instruction)) {
            if (form->operands.size() != count || index >= count ||
                instruction_feature_conflict(*form,
                    [&](std::string_view feature) { return subtarget.supports_registry_feature(feature); })) continue;
            const auto& operand = form->operands[index];
            if (patch_operand_accepts_type(operand, name, bits)) candidates.push_back({
                patch_address_type(operand.patch_address, current.address_bits), operand.patch_supports_symbol_relocation,
                EvaluationInstructionFormId{static_cast<std::size_t>(form - target->instructions.data())}});
        }
        return candidates;
    };
    current.evaluation_instruction_source = [target, &subtarget](std::string_view name,
        std::span<const EvaluationInstructionOperand> operands) {
        return instruction_source_error(*target, subtarget, name, operands);
    };
    current.evaluation_member_layout = [layout_for](
        const TypePtr& owner, const MemberName& name)
        -> ContinuationTask<std::optional<EvaluationMemberLayout>> {
        const auto layout = co_await layout_for(owner);
        if (!layout) co_return {};
        const auto id = layout->intern_type(owner);
        const auto& type = layout->type(id);
        if (!type.record) co_return std::nullopt;
        const auto* member = layout->member(*type.record, name);
        if (!member) co_return std::nullopt;
        co_return EvaluationMemberLayout{
            member->offset, member->alignment,
            member->bit_width, member->bit_offset};
    };
    current.evaluation_initializer_plan = [layout_for, target, &current](
        const Expr& expression, const TypePtr& destination) -> ContinuationTask<EvaluationInitializerPlan> {
        const bool dynamic_array = destination && destination->kind == Type::Kind::Array &&
            destination->lanes == 0;
        const auto layout = co_await layout_for(dynamic_array ? destination->element : destination);
        if (!layout) co_return EvaluationInitializerPlan{.items = {}, .valid = false,
            .error_location = expression.location,
            .error_message = "target layout is unavailable for this initializer"};
        co_return initializer::build_for_evaluation(expression, destination, current, *layout, *target);
    };
    current.evaluation_initializer_types = [layout_for, target, &current](
        const Expr& expression, const TypePtr& destination, std::span<const Expr* const> deferred)
        -> ContinuationTask<EvaluationInitializerTypePlan> {
        // Type selection must not demand physical record layout. Keep the
        // resolved type/ABI registry, but acquire no destination byte offsets.
        const auto layout = co_await layout_for(builtin_type(BuiltinType::U8));
        if (!layout) co_return EvaluationInitializerTypePlan{.items = {}, .valid = false,
            .error_location = expression.location,
            .error_message = "target layout is unavailable for this initializer"};
        co_return initializer::types_for_evaluation(expression, destination, current, *layout, *target, deferred);
    };
}

// The last demand-driven view, valid while the record table is unchanged. A
// query it does not cover extends it in the same evaluation layout scope.
struct CompletedEvaluationLayoutView {
    std::shared_ptr<hir::Module> layout;
    // Proofs retain private views, so they last only as long as the view.
    RecordSourceProofs proofs;
    const RecordDecl* records{};
    std::size_t count{};

    std::shared_ptr<hir::Module> find(const Program& program, const TypePtr& type) {
        if (records != program.records.data() || count != program.records.size()) layout.reset();
        return layout && hir::extended_layout_view_covers(*layout, program, type) ? layout : nullptr;
    }

    // After a missed find: the view to extend if it has the current scope and
    // no caller still reads it; otherwise the query starts a new view.
    std::optional<hir::Module> seed(const Program& program) {
        if (!layout || layout.use_count() != 1 || !layout->evaluation_layout_scope ||
            layout->evaluation_layout_scope != program.evaluation_layout_scope) {
            proofs = {};
            return std::nullopt;
        }
        auto view = std::move(*layout);
        layout.reset();
        return view;
    }

    // An extension replaces the view it extends; a new view replaces the kept
    // one only if it covers its query.
    void retain(const Program& program, const TypePtr& type, std::shared_ptr<hir::Module> view, bool extended) {
        if (!extended && !hir::layout_view_covers(*view, program, type)) return;
        layout = std::move(view);
        records = program.records.data();
        count = program.records.size();
    }
};

void install_early_evaluation_layout(Program& program, const CompilerOptions& options,
                                     const TargetInfo* target, const Subtarget& subtarget) {
    // Source publication and exact private graph ownership bound view reuse.
    // Preparation and cycle/resource checks still precede each query.
    using Query = std::pair<TypePtr, EvaluationLayoutKind>;
    auto active_layout_queries = std::make_shared<std::vector<Query>>();
    auto completed = std::make_shared<CompletedEvaluationLayoutView>();
    install_evaluation_layout_queries(program, target, subtarget, [&, target, active_layout_queries, completed](
        TypePtr type, EvaluationLayoutKind kind = EvaluationLayoutKind::Complete) -> ContinuationTask<std::shared_ptr<hir::Module>> {
        if (active_layout_queries->size() >= program.evaluation_limits.depth ||
            std::any_of(active_layout_queries->begin(), active_layout_queries->end(),
                [&](const auto& active) { return active.second == kind && same_type(active.first, type); })) co_return nullptr;
        active_layout_queries->emplace_back(type, kind);
        struct Pop { std::vector<Query>& values; ~Pop() { values.pop_back(); } }
            pop{*active_layout_queries};
        const auto prepare_type = program.evaluation_prepare_type;
        if (prepare_type && !(co_await prepare_type.async(type, kind))) co_return nullptr;
        const auto prepare = program.evaluation_prepare_layout;
        if (prepare && !(co_await prepare.async(type, kind))) co_return nullptr;
        if (const auto cached = completed->find(program, type)) co_return cached;
        std::ostringstream output;
        Diagnostics quiet(output);
        auto seed = completed->seed(program);
        const bool extended = seed.has_value();
        auto layout = std::make_shared<hir::Module>(co_await hir::build_required_layout_context_async(
            program, options, *target, quiet, type, kind, std::move(seed).value_or(hir::Module{}), completed->proofs));
        if (quiet.errors() != 0) co_return nullptr;
        completed->retain(program, type, layout, extended);
        co_return layout;
    });
}

void print_targets() {
    for (const auto* target : all_targets()) {
        std::cout << target->architecture << "  triples:";
        for (const auto prefix : target->triple_prefixes) std::cout << ' ' << prefix << "-*";
        std::cout << '\n';
    }
}

void print_abis(const CompilerOptions& options) {
    const auto* target = target_for_triple(options.target);
    if (!target) return;
    std::cout << target->architecture << " ABI registry for " << options.target << ":\n";
    for (const auto& abi : model_registry().abis()) {
        if (abi.architecture != target->architecture) continue;
        std::cout << "  " << abi.canonical_name << "  aliases:";
        for (const auto& alias : abi.aliases) std::cout << ' ' << alias;
        std::cout << "  compilation-selectable: " << (abi.compilation_selectable ? "yes" : "no")
                  << "  function-selectable: " << (abi.function_selectable ? "yes" : "no")
                  << "  banks: " << abi.banks.size()
                  << "  rules: " << abi.rules.size() << '\n';
    }
    std::cout << "  default: " << target->default_abi(options.target) << '\n';
}

void print_models() {
    for (const auto& origin : model_registry().origins()) {
        std::cout << origin << '\n';
    }
    for (const auto& debug : model_registry().debugs()) {
        std::cout << "debug " << debug.canonical_name
                  << "  format: dwarf  version: " << debug.version
                  << "  frame_section: "
                  << (debug.eh_frame ? "eh_frame" : "debug_frame")
                  << "  lines: " << (debug.lines ? "on" : "off")
                  << "  frames: " << (debug.frames ? "on" : "off")
                  << "  variables: " << (debug.variables ? "on" : "off")
                  << "  types: " << (debug.types ? "on" : "off")
                  << "  source: " << debug.source << '\n';
    }
}

void print_profiles() {
    for (const auto& profile : model_registry().profiles()) {
        std::cout << profile.canonical_name;
        if (profile.target) std::cout << "  target: " << *profile.target;
        if (profile.abi) std::cout << "  abi: " << *profile.abi;
        if (profile.mangling) {
            std::cout << "  mangling: " << *profile.mangling;
        }
        if (profile.optimization) {
            std::cout << "  optimization: " << *profile.optimization;
        }
        if (profile.debug) std::cout << "  debug: " << *profile.debug;
        std::cout << '\n';
        for (const auto& option : profile.options) {
            std::cout << "    " << option.name << " = "
                      << option_value_text(option.value) << '\n';
        }
        using floating::Exception;
        const auto& environment = profile.floating_environment;
        std::string traps;
        for (const auto exception :
             {Exception::Invalid, Exception::DivideByZero, Exception::Overflow,
              Exception::Underflow, Exception::Inexact}) {
            if ((environment.traps & floating::exception_set(exception)) == 0) {
                continue;
            }
            if (!traps.empty()) traps += ", ";
            traps += floating::exception_name(exception);
        }
        if (!traps.empty()) std::cout << "    fp_traps = " << traps << '\n';
        if ((environment.traps &
             floating::exception_set(Exception::DenormalOperand)) != 0) {
            std::cout << "    fp_denormal_operand = trap\n";
        }
        if (environment.flush_denormal_results) {
            std::cout << "    fp_denormal_result = flush\n";
        }
    }
}

void print_manglings() {
    for (const auto& mangling : model_registry().manglings()) {
        std::cout << mangling.canonical_name
                  << "  rules: entity, label, generic\n";
    }
}

void print_optimizations() {
    for (const auto& optimization : model_registry().optimizations()) {
        std::cout << optimization.canonical_name;
        if (optimization.inherits) {
            std::cout << "  inherits: " << *optimization.inherits;
        }
        if (!optimization.targets.empty()) {
            std::cout << "  targets:";
            for (const auto& target : optimization.targets) {
                std::cout << ' ' << target;
            }
        }
        std::cout << '\n';
        for (const auto& option : optimization.options) {
            std::cout << "  " << option.name << " = "
                      << option_value_text(option.value) << '\n';
        }
    }
}

void print_options(const CompilerOptions& options) {
    std::vector<const OptionDefinition*> definitions;
    for (const auto& definition : common_option_definitions()) {
        definitions.push_back(&definition);
    }
    if (const auto* target = target_for_triple(options.target)) {
        for (const auto& definition : target->options) {
            definitions.push_back(&definition);
        }
    }
    std::sort(definitions.begin(), definitions.end(),
              [](const OptionDefinition* left,
                 const OptionDefinition* right) {
                  return left->name < right->name;
              });
    for (const auto* definition : definitions) {
        const auto& category = options.print_options_category;
        if (category == "optimization" &&
            definition->category != OptionCategory::Optimization) {
            continue;
        }
        if (category == "common" &&
            definition->name.starts_with("m.")) {
            continue;
        }
        if (category == "target" &&
            !definition->name.starts_with("m.")) {
            continue;
        }
        const auto* resolved =
            find_resolved_option(options, definition->name);
        std::cout << definition->name
                  << "  type: " << option_type_text(*definition)
                  << "  value: "
                  << (resolved ? option_value_text(resolved->value)
                               : option_value_text(definition->default_value))
                  << "  origin: "
                  << (resolved ? option_origin_name(resolved->origin)
                               : "default")
                  << "  state: "
                  << (definition->implementation ==
                              OptionImplementation::Implemented
                          ? "implemented"
                          : "partial")
                  << "  presettable: "
                  << (definition->presettable ? "yes" : "no");
        if (resolved && resolved->source != "default") {
            std::cout << "  source: " << resolved->source;
        }
        std::cout << '\n';
    }
}

void print_attributes() {
    for (const auto attribute : core_attribute_names()) {
        std::cout << attribute << '\n';
    }
}

void print_registers(const CompilerOptions& options) {
    const auto* target = target_for_triple(options.target);
    if (!target) return;
    std::cout << target->architecture << " register registry for " << options.target << ":\n";
    for (const auto& reg : target->registers) {
        std::cout << "  " << reg.name << "  storage: " << reg.storage
                  << "  bits: " << reg.bits << "  class: " << reg.register_class
                  << "  feature: " << reg.feature;
        if (reg.address_capable) std::cout << "  address";
        std::cout << '\n';
    }
}

void print_keywords() {
    for (const auto keyword : core_keyword_names()) std::cout << keyword << '\n';
}

void print_target_instructions(const TargetInfo& target) {
    std::unordered_set<std::string> emitted;
    for (const auto& instruction : target.instructions) {
        std::ostringstream line;
        line << instruction.name << " instruction [" << instruction.feature;
        for (const auto feature : instruction.required_features) {
            line << ',' << feature;
        }
        for (const auto feature : instruction.forbidden_features) {
            line << ",!" << feature;
        }
        line << ']';
        if (instruction.privileged) line << " privileged";
        if (instruction.volatile_effect) line << " volatile";
        const auto text = std::move(line).str();
        if (emitted.insert(text).second) std::cout << text << '\n';
    }
}

void print_builtins(const CompilerOptions& options) {
    for (const auto& entry : core_expression_builtins())
        std::cout << entry.name << ' ' << entry.description << '\n';
    std::cout << "$::has_builtin query\n"
                 "$::has_intrinsic query\n"
                 "$::has_instruction query\n"
                 "$::has_patch_value query\n"
                 "$::has_patch_operand query\n"
                 "$::has_attribute query\n"
                 "$::has_feature query\n"
                 "$::has_extension query\n"
                 "$::has_abi query\n"
                 "$::has_mangling query\n"
                 "$::has_profile query\n"
                 "$::has_include query\n"
                 "$::language::version predefined macro\n"
                 "$::language::version_major predefined macro\n"
                 "$::language::version_minor predefined macro\n"
                 "$::source::file source-location macro\n"
                 "$::source::line source-location macro\n"
                 "$::target::triple predefined macro\n"
                 "$::target::abi predefined macro\n"
                 "$::target::mangling predefined macro\n"
                 "$::target::profile predefined macro\n"
                 "$::target::byte_bits predefined macro\n"
                 "$::target::pointer_bytes predefined macro\n"
                 "$::target::order_little predefined macro\n"
                 "$::target::order_big predefined macro\n"
                 "$::target::byte_order predefined macro\n";
    const auto* target = target_for_triple(options.target);
    if (!target) return;
    print_target_instructions(*target);
}

void print_instructions(const CompilerOptions& options) {
    const auto* target = target_for_triple(options.target);
    if (!target) return;
    print_target_instructions(*target);
}

void print_features(const CompilerOptions& options) {
    for (const auto feature : language_features(options))
        std::cout << "$::feature::" << feature << '\n';
    if (const auto* target = target_for_triple(options.target)) {
        if (const auto* table = subtarget_table_for(*target)) {
            for (const auto& feature : table->features) {
                if (!feature.selectable) continue;
                std::cout << "$::feature::" << feature.name
                          << " target extension ["
                          << (resolved_bool(options,
                                            "m." + std::string(feature.name))
                                  ? "enabled" : "disabled")
                          << "]\n";
            }
        }
    }
}

std::filesystem::path default_output(const CompilerOptions& options) {
    auto output = options.inputs.empty() ? std::filesystem::path("a") : options.inputs.front().stem();
    switch (options.emit) {
    case EmitKind::Assembly: output += ".s"; break;
    case EmitKind::Object: output += ".o"; break;
    case EmitKind::LlvmTextDebug: output += ".ll"; break;
    case EmitKind::GimpleTextDebug:
    case EmitKind::GimpleRtlTextDebug: output += ".gimple.c"; break;
    case EmitKind::Preprocess: output += ".i"; break;
    case EmitKind::Link: output += ".exe"; break;
    }
    return output;
}

const VariableDecl* scalable_local(const Statement& statement) {
    if (statement.declaration && statement.declaration->type &&
        statement.declaration->type->kind == Type::Kind::Vector &&
        statement.declaration->type->scalable) {
        return statement.declaration.get();
    }
    for (const auto& child : statement.statements) {
        if (const auto* found = scalable_local(*child)) return found;
    }
    if (statement.first) {
        if (const auto* found = scalable_local(*statement.first)) return found;
    }
    if (statement.second) {
        if (const auto* found = scalable_local(*statement.second)) return found;
    }
    return nullptr;
}

void diagnose_codegen_ownership_gaps(const hir::Module& module,
                                     Diagnostics& diagnostics) {
    for (const auto& function : module.functions) {
        if (function.definition &&
            function.ownership == hir::BodyOwnership::ManagedAst) {
            if (const auto* local = function.definition->body
                                        ? scalable_local(
                                              *function.definition->body)
                                        : nullptr) {
                diagnostics.error(
                    local->location,
                    "scalable-vector values are not supported by the "
                    "selected target backend");
                continue;
            }
            diagnostics.error(
                function.location,
                "middle end could not lower function '" +
                    function.source_name + "' to managed or raw MIR");
        }
    }
}

void track_block_comment(std::string_view line, bool& inside) {
    for (std::size_t index = 0; index < line.size();) {
        if (inside) {
            if (index + 1 < line.size() && line[index] == '*' && line[index + 1] == '/') {
                inside = false;
                index += 2;
            } else ++index;
            continue;
        }
        if (index + 1 < line.size() && line[index] == '/' && line[index + 1] == '/') break;
        if (index + 1 < line.size() && line[index] == '/' && line[index + 1] == '*') {
            inside = true;
            index += 2;
            continue;
        }
        if (line[index] == '"' || line[index] == '\'') {
            const char quote = line[index++];
            while (index < line.size() && line[index] != quote) {
                if (line[index] == '\\' && index + 1 < line.size()) ++index;
                ++index;
            }
            if (index < line.size()) ++index;
            continue;
        }
        ++index;
    }
}

// Identifies one file among dependencies; never written to output.
std::string dependency_identity(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return (error ? path.lexically_normal() : canonical).generic_string();
}

std::string unit_occurrence(std::string path,
                            std::unordered_map<std::string, unsigned>& counts) {
    const auto ordinal = ++counts[path];
    if (ordinal == 1) return path;
    return path + '\x1f' + std::to_string(ordinal);
}

std::string source_unit_marker(std::string_view unit) {
    std::ostringstream output;
    output << "#$::source::unit " << std::quoted(std::string(unit)) << '\n';
    return output.str();
}

std::string with_line_markers(std::string_view source,
                              const std::vector<SourceLocation>& origins,
                              const std::filesystem::path& primary) {
    std::istringstream input{std::string(source)};
    std::ostringstream output;
    std::string line;
    std::filesystem::path previous;
    unsigned previous_line = 0;
    bool inside_comment = false;
    std::size_t index = 0;
    while (std::getline(input, line)) {
        const auto origin = index < origins.size() ? origins[index] : SourceLocation{};
        const auto path = origin.file ? origin.file->path : primary;
        const auto number = origin.file ? origin.line : static_cast<unsigned>(index + 1);
        if (!inside_comment &&
            (index == 0 || path != previous || number != previous_line + 1))
            output << "#line " << number << ' ' << std::quoted(path.generic_string()) << '\n';
        output << line << '\n';
        if (!inside_comment) {
            previous = path;
            previous_line = number;
        }
        track_block_comment(line, inside_comment);
        ++index;
    }
    return output.str();
}

std::string without_line_markers(SourceManager& sources, const SourceFile& source,
                                 std::vector<SourceLocation>& origins,
                                 std::vector<std::string>& units,
                                 std::string unit,
                                 Diagnostics& diagnostics) {
    std::istringstream input(source.text);
    std::ostringstream output;
    std::unordered_map<std::string, const SourceFile*> named;
    std::unordered_map<std::string, unsigned> unit_counts;
    const SourceFile* logical = &source;
    unsigned logical_line = 1;
    unsigned physical_line = 0;
    bool inside_comment = false;
    std::string line;
    while (std::getline(input, line)) {
        ++physical_line;
        if (!inside_comment && line.starts_with("#$::source::unit")) {
            std::istringstream marker(line.substr(16));
            marker >> std::ws;
            std::string path;
            if (marker.peek() == '"') marker >> std::quoted(path);
            marker >> std::ws;
            if (!marker || !marker.eof() || path.empty()) {
                diagnostics.error({&source, 0, physical_line, 1},
                                  "malformed preprocessed source-unit boundary");
            } else unit = unit_occurrence(std::move(path), unit_counts);
            continue;
        }
        if (!inside_comment && line.starts_with("#line") &&
            (line.size() == 5 ||
             std::isspace(static_cast<unsigned char>(line[5])) != 0)) {
            std::istringstream marker(line.substr(5));
            unsigned number = 0;
            std::string path;
            marker >> std::ws;
            const auto first = marker.peek();
            if (first >= 0 && std::isdigit(static_cast<unsigned char>(first)) != 0)
                marker >> number >> std::ws;
            if (marker.peek() == '"') marker >> std::quoted(path);
            marker >> std::ws;
            if (!marker || !marker.eof() || number == 0 || path.empty()) {
                diagnostics.error({&source, 0, physical_line, 1},
                                  "malformed preprocessed #line marker");
                continue;
            }
            auto [entry, inserted] = named.try_emplace(path, nullptr);
            if (inserted) entry->second = sources.add(path, "");
            logical = entry->second;
            logical_line = number;
            continue;
        }
        output << line << '\n';
        origins.push_back({logical, 0, logical_line, 1});
        units.push_back(unit);
        track_block_comment(line, inside_comment);
        if (logical_line != std::numeric_limits<unsigned>::max()) ++logical_line;
    }
    return output.str();
}

bool preprocess_inputs(const CompilerOptions& options, SourceManager& sources,
                       Diagnostics& diagnostics, std::vector<std::string>& outputs,
                       std::vector<std::vector<std::filesystem::path>>& dependencies,
                       std::vector<const SourceFile*>& preprocessed_sources,
                       bool compiler) {
    std::unordered_map<std::string, unsigned> unit_counts;
    EmbedSnapshots snapshots;
    for (const auto& input : options.inputs) {
        // A source unit is identified by its primary input's path as written,
        // after -ffile-prefix-map, never by a path the compiler resolved.
        const auto unit = mapped_source_path(options, input);
        const SourceFile* preprocessed{};
        std::vector<SourceLocation> line_origins;
        std::vector<std::string> line_units;
        std::vector<std::filesystem::path> found;
        std::string serialized;
        if (input.extension() == ".i") {
            std::string error;
            const auto* written = sources.load(input, error);
            if (!written) diagnostics.command_error(error);
            else {
                serialized = written->text;
                auto source = without_line_markers(sources, *written,
                                                   line_origins, line_units,
                                                   unit_occurrence(unit, unit_counts),
                                                   diagnostics);
                preprocessed = sources.add(input, std::move(source), {}, {},
                                           line_origins, line_units);
                found.push_back(input);
            }
        } else {
            Preprocessor preprocessor(sources, diagnostics, options);
            auto source = preprocessor.process(input);
            found = preprocessor.dependencies();
            line_origins = preprocessor.output_line_locations();
            serialized = with_line_markers(source, line_origins, input);
            line_units.assign(line_origins.size(),
                              unit_occurrence(unit, unit_counts));
            preprocessed = sources.add(input, std::move(source), {}, {},
                                       line_origins, line_units);
        }
        if (!preprocessed) continue;
        if (options.inputs.size() > 1)
            serialized.insert(0, source_unit_marker(unit));
        outputs.push_back(std::move(serialized));
        if (diagnostics.errors() == 0) {
            auto embedded = discover_embeds(sources, *preprocessed, line_origins,
                options, diagnostics,
                options.dependency_mode != DependencyMode::None ||
                (compiler && options.emit != EmitKind::Preprocess), &snapshots);
            preprocessed = embedded.source;
            std::unordered_set<std::string> seen;
            for (const auto& path : found) seen.insert(dependency_identity(path));
            for (const auto& path : embedded.dependencies)
                if (seen.insert(path.generic_string()).second) found.push_back(path);
        }
        dependencies.push_back(std::move(found));
        preprocessed_sources.push_back(preprocessed);
    }
    return diagnostics.errors() == 0;
}

std::vector<const SourceFile*> split_source_units(
    SourceManager& sources, const SourceFile& source) {
    if (source.line_units.empty()) return {&source};
    std::vector<const SourceFile*> result;
    for (std::size_t begin_line = 0; begin_line < source.line_units.size();) {
        auto end_line = begin_line + 1;
        while (end_line < source.line_units.size() &&
               source.line_units[end_line] == source.line_units[begin_line])
            ++end_line;
        if (begin_line == 0 && end_line == source.line_units.size())
            return {&source};
        const auto begin = source.line_starts[begin_line];
        const auto end = source.line_starts[end_line];
        const auto first = static_cast<std::ptrdiff_t>(begin_line);
        const auto last = static_cast<std::ptrdiff_t>(end_line);
        std::vector<SourceTokenOrigin> origins;
        for (const auto& token : source.token_origins) {
            if (token.begin < begin || token.end > end) continue;
            auto shifted = token;
            shifted.begin -= begin;
            shifted.end -= begin;
            origins.push_back(std::move(shifted));
        }
        result.push_back(sources.add(source.path,
            source.text.substr(begin, end - begin), {}, std::move(origins),
            std::vector<SourceLocation>(source.line_origins.begin() + first,
                                        source.line_origins.begin() + last),
            std::vector<std::string>(source.line_units.begin() + first,
                                     source.line_units.begin() + last)));
        begin_line = end_line;
    }
    return result;
}

std::string make_escape(std::string_view spelling) {
    std::string result;
    for (const char ch : spelling) {
        if (ch == ' ' || ch == '#' || ch == ':') result += '\\';
        if (ch == '$') result += '$';
        result += ch;
    }
    return result;
}

// cpp writes one rule per input; cc writes one rule for the single output of
// its group, whose prerequisites are those of every input.
std::string dependency_text(
    const CompilerOptions& options,
    const std::vector<std::vector<std::filesystem::path>>& dependencies,
    bool compiler) {
    std::vector<std::vector<std::filesystem::path>> rules;
    if (!compiler) {
        rules = dependencies;
    } else {
        std::unordered_set<std::string> seen;
        auto& group = rules.emplace_back();
        for (const auto& input : dependencies)
            for (const auto& path : input)
                if (seen.insert(dependency_identity(path)).second) group.push_back(path);
    }
    std::ostringstream output;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        if (!options.dependency_targets.empty()) {
            for (std::size_t target = 0; target < options.dependency_targets.size(); ++target) {
                if (target != 0) output << ' ';
                const auto& spelling = options.dependency_targets[target];
                output << (spelling.quote ? make_escape(spelling.spelling)
                                          : spelling.spelling);
            }
        } else {
            auto target = options.inputs[index].stem();
            target += ".o";
            if (compiler && options.dependency_mode == DependencyMode::Alongside &&
                options.output && options.emit != EmitKind::Preprocess)
                target = *options.output;
            output << make_escape(target.generic_string());
        }
        output << ':';
        for (const auto& path : rules[index])
            output << ' ' << make_escape(path.generic_string());
        output << '\n';
    }
    if (options.dependency_phony_targets) {
        std::unordered_set<std::string> seen;
        for (const auto& input : options.inputs) seen.insert(dependency_identity(input));
        for (const auto& rule : rules)
            for (const auto& path : rule)
                if (seen.insert(dependency_identity(path)).second)
                    output << make_escape(path.generic_string()) << ":\n";
    }
    return output.str();
}

bool emit_dependencies(
    const CompilerOptions& options,
    const std::vector<std::vector<std::filesystem::path>>& dependencies,
    bool compiler, Diagnostics& diagnostics) {
    if (options.dependency_mode == DependencyMode::None) return true;
    const auto content = dependency_text(options, dependencies, compiler);
    std::optional<std::filesystem::path> path = options.dependency_file;
    if (!path && options.dependency_mode == DependencyMode::Only)
        path = options.output;
    if (!path && options.dependency_mode == DependencyMode::Alongside) {
        if (options.output) path = *options.output;
        else path = options.inputs.front().stem();
        path->replace_extension(".d");
    }
    if (path && options.output &&
        options.dependency_mode == DependencyMode::Alongside) {
        std::error_code dependency_error, source_error;
        const auto dependency_identity =
            std::filesystem::weakly_canonical(*path, dependency_error);
        const auto source_identity =
            std::filesystem::weakly_canonical(*options.output, source_error);
        if (!dependency_error && !source_error &&
            dependency_identity == source_identity) {
            diagnostics.command_error(
                "source output and dependency file must be different paths");
            return false;
        }
    }
    if (!path) {
        std::cout << content;
        return true;
    }
    std::string error;
    if (!write_file(*path, content, error)) {
        diagnostics.command_error(error);
        return false;
    }
    return true;
}

} // namespace

int cpp_main(int argc, char** argv) {
    Diagnostics diagnostics(std::cerr, "cpp");
    CompilerOptions options;
    if (!parse_cpp_options(argc, argv, options, diagnostics)) return 1;
    if (options.show_help) { print_cpp_help(); return 0; }
    if (options.show_version) { print_version(); return 0; }
    if (!configure_models(options, diagnostics)) return 1;
    if (options.inputs.empty()) {
        diagnostics.command_error("no input files");
        return 1;
    }
    SourceManager sources;
    std::vector<std::string> outputs;
    std::vector<std::vector<std::filesystem::path>> dependencies;
    std::vector<const SourceFile*> preprocessed_sources;
    if (!preprocess_inputs(options, sources, diagnostics, outputs, dependencies,
                           preprocessed_sources, false)) return 1;
    if (!emit_dependencies(options, dependencies, false, diagnostics)) return 1;
    if (options.dependency_mode == DependencyMode::Only) return 0;
    std::ostringstream joined;
    for (const auto& output : outputs) joined << output;
    if (options.output) {
        std::string error;
        if (!write_file(*options.output, joined.str(), error)) {
            diagnostics.command_error(error);
            return 1;
        }
    } else {
        std::cout << joined.str();
    }
    return 0;
}

int cc_main(int argc, char** argv) {
    Diagnostics diagnostics(std::cerr);
    CompilerOptions options;
    if (!parse_cc_options(argc, argv, options, diagnostics)) return 1;
    if (options.show_help) { print_cc_help(); return 0; }
    if (options.show_version) { print_version(); return 0; }
    if (!configure_models(options, diagnostics)) return 1;
    if (options.print_targets) { print_targets(); return 0; }
    if (options.print_abis) { print_abis(options); return 0; }
    if (options.print_models) { print_models(); return 0; }
    if (options.print_profiles) { print_profiles(); return 0; }
    if (options.print_manglings) { print_manglings(); return 0; }
    if (options.print_optimizations) { print_optimizations(); return 0; }
    if (options.print_options) { print_options(options); return 0; }
    if (options.print_attributes) { print_attributes(); return 0; }
    if (options.print_registers) { print_registers(options); return 0; }
    if (options.print_keywords) { print_keywords(); return 0; }
    if (options.print_builtins) { print_builtins(options); return 0; }
    if (options.print_instructions) { print_instructions(options); return 0; }
    if (options.print_features) { print_features(options); return 0; }
    if (options.inputs.empty()) {
        diagnostics.command_error("no input files");
        return 1;
    }
    if (!target_for_triple(options.target)) {
        diagnostics.command_error(
            (options.target_explicit || options.profile_explicit ? "target '" : "default target '") +
            options.target + "' is not implemented; use --print-targets to list compiled-in targets");
        return 1;
    }
    if (options.emit == EmitKind::Link &&
        options.dependency_mode != DependencyMode::Only) {
        diagnostics.command_error("cc does not link; use -S, -c, or -E");
        return 1;
    }
    if (options.debug_info && (options.emit == EmitKind::LlvmTextDebug ||
                               options.emit == EmitKind::GimpleTextDebug ||
                               options.emit == EmitKind::GimpleRtlTextDebug)) {
        diagnostics.command_error(
            "-g is not implemented for -emit-llvm and -emit-gimple output");
        return 1;
    }

    SourceManager sources;
    std::vector<std::string> preprocessed;
    std::vector<std::vector<std::filesystem::path>> dependencies;
    std::vector<const SourceFile*> preprocessed_sources;
    if (!preprocess_inputs(options, sources, diagnostics, preprocessed, dependencies,
                           preprocessed_sources, true)) return 1;
    if (!emit_dependencies(options, dependencies, true, diagnostics)) return 1;
    if (options.dependency_mode == DependencyMode::Only) return 0;
    if (options.emit == EmitKind::Preprocess) {
        std::ostringstream joined;
        for (const auto& source : preprocessed) joined << source;
        const auto output = options.output.value_or(default_output(options));
        std::string error;
        if (!write_file(output, joined.str(), error)) diagnostics.command_error(error);
        return diagnostics.errors() == 0 ? 0 : 1;
    }

    const auto* target = target_for_triple(options.target);
    auto subtarget = resolve_subtarget(*target, options, diagnostics);
    if (!subtarget) return 1;
    const GenericAbiCanonicalizer canonical_abi =
        [&](std::string_view name) -> std::optional<std::string> {
            const auto* abi = find_abi(*target,
                name.empty() ? std::string_view(options.abi) : name,
                options.target);
            return abi ? std::optional<std::string>(abi->canonical_name)
                       : std::nullopt;
        };
    const EvaluationLayoutInstaller install_expansion_evaluation = [&](Program& current) {
        install_early_evaluation_layout(current, options, target, *subtarget);
        current.evaluation_pointer_resolver = [&current, &options, &subtarget, &diagnostics](
            std::unique_ptr<Expr>& expression, const TypePtr& destination,
            const FunctionDecl* caller, std::span<const NameKey> locals) {
            return data::normalize_generic_pointer_async(current, expression, destination,
                caller, locals, options, *subtarget, diagnostics);
        };
    };
    Program program;
    program.canonical_callable_abi = canonical_abi;
    program.address_bits = subtarget->abi_info().address_bits;
    program.evaluation_limits = {
        options.eval_byte_limit, options.eval_memory_limit,
        options.eval_step_limit, options.eval_depth_limit,
        options.generic_instance_limit, options.generic_depth_limit};
    program.evaluation_layout = {
        target->data_layout.byte_order == ByteOrder::Big
            ? EvaluationByteOrder::Big : EvaluationByteOrder::Little,
        target->data_layout.natural_alignment_limit,
        target->data_layout.f80_storage_bytes,
        target->data_layout.f80_alignment,
        target->data_layout.code_addresses,
        options.wrapv,
        options.floating_environment};
    // Macro execution precedes source declarations. Scalar layout queries
    // nevertheless use the same target-owned context as later required folds.
    auto macro_layout = hir::build_constant_context(program, options, *target, diagnostics);
    const LayoutQuery macro_size = [&](const TypePtr& type) {
        return hir::layout_size(macro_layout, macro_layout.intern_type(type), *target);
    };
    const LayoutQuery macro_align = [&](const TypePtr& type) {
        return hir::layout_alignment(macro_layout, macro_layout.intern_type(type), *target);
    };
    std::vector<const SourceFile*> compilation_units;
    for (const auto* source : preprocessed_sources) {
        auto sections = split_source_units(sources, *source);
        compilation_units.insert(compilation_units.end(), sections.begin(), sections.end());
    }
    for (const auto* unit : compilation_units)
        program.source_units.push_back(unit->source_unit_at(1));
    for (const auto* preprocessed_source : compilation_units) {
        auto tokens = Lexer(*preprocessed_source, diagnostics).lex();
        // One expansion path for every source unit. An unused syntax
        // declaration cannot change procedural token-splice semantics.
        // Parser-driven exposure keeps nested raw input behind its owner.
        if (!validate_embeds(*preprocessed_source, diagnostics)) return 1;
        std::shared_ptr<SyntaxExecution> execution;
        for (std::size_t at = 0; at < tokens.size(); ++at) {
            const bool invocation = at + 2 < tokens.size() &&
                tokens[at].kind == TokenKind::Identifier && tokens[at + 1].is("!") &&
                (tokens[at + 2].is("(") || tokens[at + 2].is("[") || tokens[at + 2].is("{"));
            // A role attribute list begins with "[["; starting the head scan
            // at every token would be quadratic in a long statement.
            if (!tokens[at].is("syntax") && !invocation &&
                !(tokens[at].is("[[") && expansion_function_head(tokens, at))) continue;
            // Ordinary units need no expansion environment or meta budget.
            // This scan only enables the engine; it registers nothing.
            execution = std::make_shared<SyntaxExecution>(sources, diagnostics,
                program.address_bits, macro_size, macro_align,
                program.evaluation_limits, program.evaluation_layout,
                install_expansion_evaluation, options.mangling, canonical_abi);
            break;
        }
        if (diagnostics.errors() != 0) return 1;
        Parser parser(std::move(tokens), diagnostics, std::move(execution),
                      program.address_bits);
        auto unit = parser.parse();
        for (auto& record : unit.records) {
            program.records.push_back(std::move(record));
        }
        for (auto& enumeration : unit.enumerations) {
            program.enumerations.push_back(std::move(enumeration));
        }
        for (auto& assertion : unit.static_assertions) {
            program.static_assertions.push_back(std::move(assertion));
        }
        for (auto& type : unit.required_types) program.required_types.push_back(std::move(type));
        for (auto& label : unit.global_labels) {
            program.global_labels.push_back(std::move(label));
        }
        for (auto& function : unit.functions) program.functions.push_back(std::move(function));
        for (auto& object : unit.objects) program.objects.push_back(std::move(object));
    }
    if (diagnostics.errors() != 0) return 1;
    if (!hir::validate_source_address_spaces(
            program, options, *target, diagnostics)) return 1;
    if (options.verbose) std::cerr << "cc: expanding generics and compile-time evaluation\n";
    const GenericPointerResolver pointer_resolver =
        [&](std::unique_ptr<Expr>& expression, const TypePtr& destination,
            const FunctionDecl* caller, std::span<const NameKey> locals) {
            return data::normalize_generic_pointer_async(program, expression, destination,
                caller, locals, options, *subtarget, diagnostics);
        };
    program.evaluation_pointer_resolver = [&](std::unique_ptr<Expr>& expression,
        const TypePtr& destination, const FunctionDecl* caller, std::span<const NameKey> locals) -> ContinuationTask<bool> {
        // An automatic attempt may defer to runtime without speculative diagnostics.
        std::ostringstream output;
        Diagnostics quiet(output);
        co_return co_await data::normalize_generic_pointer_async(program, expression, destination,
            caller, locals, options, *subtarget, quiet);
    };
    install_early_evaluation_layout(program, options, target, *subtarget);
    const EvaluationLayoutInstaller install_layout = [&](Program& current) {
        auto layout = std::make_shared<hir::Module>(
            hir::build_record_layout_context(current, options, *target, diagnostics));
        if (diagnostics.errors() != 0) return;
        auto completed = std::make_shared<CompletedEvaluationLayoutView>();
        install_evaluation_layout_queries(current, target, *subtarget, [&, layout, completed](
            TypePtr type, EvaluationLayoutKind kind = EvaluationLayoutKind::Complete) -> ContinuationTask<std::shared_ptr<hir::Module>> {
            const auto prepare = current.evaluation_prepare_layout;
            if (prepare && !(co_await prepare.async(type, kind))) co_return nullptr;
            // Published records use the settled view. A separate completed
            // demand-driven view is reusable only in its exact private scope.
            if (hir::layout_view_covers(*layout, current, type)) co_return layout;
            if (const auto cached = completed->find(current, type)) co_return cached;
            std::ostringstream output;
            Diagnostics quiet(output);
            auto seed = completed->seed(current);
            const bool extended = seed.has_value();
            auto required = std::make_shared<hir::Module>(co_await hir::build_required_layout_context_async(
                current, options, *target, quiet, type, kind, std::move(seed).value_or(hir::Module{}), completed->proofs));
            if (quiet.errors()) co_return nullptr;
            completed->retain(current, type, required, extended);
            co_return required;
        });
    };
    if (!expand_semantics(program, diagnostics, options.evaluate_calls,
                          options.mangling, options.abi, pointer_resolver,
                          canonical_abi, install_layout)) return 1;
    const auto* backend = target_backend_for(*target);
    if (!backend) {
        diagnostics.command_error(
            "target '" + std::string(target->architecture) +
            "' has no registered production backend");
        return 1;
    }
    if (options.verbose) std::cerr << "cc: building HIR\n";
    auto hir_module = hir::build(program, options, *target, diagnostics);
    if (diagnostics.errors() != 0) return 1;
    const LayoutQuery size_of = [&](const TypePtr& type) {
        return hir::layout_size(hir_module, hir_module.intern_type(type),
                                *target);
    };
    const LayoutQuery align_of = [&](const TypePtr& type) {
        return hir::layout_alignment(
            hir_module, hir_module.intern_type(type), *target);
    };
    if (!finalize_target_constants(program, diagnostics, size_of, align_of)) {
        return 1;
    }
    if (options.verbose) {
        std::cerr << "cc: validating target HIR and ABI contracts\n";
    }
    // Continue into body lowering after target-HIR diagnostics so independent
    // function-body errors can be reported in the same invocation.
    (void)backend->validate_hir(
        hir_module, *subtarget, options, diagnostics);
    if (options.verbose) std::cerr << "cc: lowering static data IR\n";
    auto data_module = data::lower(hir_module, *subtarget, diagnostics);
    if (options.verbose) std::cerr << "cc: lowering raw MIR\n";
    auto raw_mir = backend->lower_raw(
        hir_module, *subtarget, diagnostics);
    if (options.verbose) std::cerr << "cc: lowering managed SSA MIR\n";
    auto managed_mir = mir::lower_managed(
        hir_module, *subtarget, options, diagnostics);
    if (diagnostics.errors() != 0) return 1;
    mir::optimize(
        managed_mir, hir_module, *subtarget, options, diagnostics);
    if (!mir::verify(managed_mir, hir_module, diagnostics)) return 1;
    if (options.verbose) {
        std::cerr << "cc: validating target ABI plans and managed legality\n";
    }
    if (!backend->prepare_managed(
            managed_mir, hir_module, *subtarget, options, diagnostics)) {
        return 1;
    }
    if (options.verbose) std::cerr << "cc: emitting raw machine assembly\n";
    native::DebugInfo debug(options, hir_module, managed_mir,
                            program.enumerations, *subtarget);
    auto raw_assembly = backend->emit_raw_assembly_with_debug(
        raw_mir, managed_mir, hir_module, *subtarget, options, debug,
        diagnostics);
    if (diagnostics.errors() != 0) return 1;
    const codegen::ModuleView patch_data_module{
        hir_module, data_module, managed_mir, raw_mir, raw_assembly};
    raw_assembly.module_assembly += native::emit_patch_data_assembly(
        patch_data_module, *subtarget, options, diagnostics);
    if (diagnostics.errors() != 0) return 1;
    const codegen::ModuleView codegen_module{
        hir_module, data_module, managed_mir, raw_mir, raw_assembly};
    if (!codegen::verify(codegen_module, diagnostics)) return 1;
    if (!codegen_module.fully_lowered()) {
        diagnose_codegen_ownership_gaps(hir_module, diagnostics);
        return 1;
    }

    const auto output = options.output.value_or(default_output(options));
    if (options.emit == EmitKind::LlvmTextDebug) {
#if CROSS_ENABLE_LLVM_TEXT
        if (options.verbose) {
            std::cerr << "cc: serializing debug/compatibility LLVM IR\n";
        }
        debug::LlvmTextSerializer serializer(options, diagnostics);
        const auto ir = serializer.serialize(codegen_module);
        if (diagnostics.errors() != 0) return 1;
        std::string error;
        if (!write_file(output, ir, error)) diagnostics.command_error(error);
#else
        diagnostics.command_error(
            "LLVM text serialization is unavailable in this build; "
            "configure with -DCROSS_ENABLE_LLVM_TEXT=ON");
#endif
    } else if (options.emit == EmitKind::GimpleTextDebug ||
               options.emit == EmitKind::GimpleRtlTextDebug) {
#if CROSS_ENABLE_GCC_GIMPLE_TEXT
        if (options.verbose) {
            std::cerr << "cc: serializing GCC GIMPLE backend-validation input\n";
        }
        const auto start = options.emit == EmitKind::GimpleRtlTextDebug
            ? debug::GimpleStart::Rtl
            : debug::GimpleStart::Gimple;
        debug::GimpleTextSerializer serializer(options, diagnostics, start);
        const auto source = serializer.serialize(codegen_module);
        if (diagnostics.errors() != 0) return 1;
        std::string error;
        if (!write_file(output, source, error)) diagnostics.command_error(error);
#else
        diagnostics.command_error(
            "GCC GIMPLE serialization is unavailable in this build; "
            "configure with -DCROSS_ENABLE_GCC_GIMPLE_TEXT=ON");
#endif
    } else {
        if (options.verbose) {
            std::cerr << "cc: selecting native " << backend->architecture()
                      << " Machine IR and emitting target assembly\n";
        }
        auto managed = backend->emit_managed_assembly(
            managed_mir, hir_module, *subtarget, options, debug, diagnostics);
        auto functions = std::move(managed.functions);
        functions.insert(functions.end(), raw_assembly.functions.begin(),
                         raw_assembly.functions.end());
        auto assembly = std::move(managed.header) +
            native::emit_module_assembly(codegen_module, std::move(functions),
                                         *subtarget, options, diagnostics);
        assembly = debug.finish(std::move(assembly));
        if (subtarget->object_format() == ObjectFormat::MachO) {
            assembly += ".subsections_via_symbols\n";
        }
        if (diagnostics.errors() != 0) return 1;
        if (options.emit == EmitKind::Assembly) {
            std::string error;
            if (!write_file(output, assembly, error)) {
                diagnostics.command_error(error);
            }
        } else {
            const auto object_writer_cpu =
                backend->object_writer_cpu(*subtarget);
            const auto object_writer_features =
                backend->object_writer_features(*subtarget);
            native::AssemblyRequest request{
                assembly, options.target,
                object_writer_cpu,
                &object_writer_features, subtarget->object_format(), output,
                options.verbose,
                options.save_temps};
            if (native::assemble_object(request, diagnostics)) {
                (void)backend->finalize_object(output, *subtarget,
                                               diagnostics);
            }
        }
    }
    return diagnostics.errors() == 0 ? 0 : 1;
}

} // namespace cross
