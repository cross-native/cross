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
#include "common/options.hpp"
#include "common/source.hpp"
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/preprocessor.hpp"
#include "frontend/procedural.hpp"
#include "frontend/semantic.hpp"
#include "middle/hir.hpp"
#include "middle/codegen_module.hpp"
#include "middle/data_ir.hpp"
#include "middle/mir.hpp"
#include "model/model.hpp"
#include "target/backend.hpp"
#include "target/target.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>

namespace cross {
namespace {

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
        std::cout << '\n';
        for (const auto& option : profile.options) {
            std::cout << "    " << option.name << " = "
                      << option_value_text(option.value) << '\n';
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
            !definition->name.starts_with("f.")) {
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
    static constexpr std::string_view keywords[] = {
        "bool", "break", "case", "const", "continue", "default", "do",
        "else", "enum", "f32", "f64", "f80", "f128", "for", "fptr",
        "global", "goto", "i8", "i16", "i32", "i64", "i128", "if",
        "in", "inline", "inout", "iptr", "label", "namespace", "out",
        "register", "restrict", "return", "sizeof", "stack", "static",
        "struct", "switch", "typedef", "u8", "u16", "u32", "u64",
        "union", "u128", "uptr", "using", "void", "volatile", "while",
    };
    for (const auto keyword : keywords) std::cout << keyword << '\n';
}

void print_target_instructions(const TargetInfo& target) {
    std::unordered_set<std::string> emitted;
    for (const auto& instruction : target.instructions) {
        std::ostringstream line;
        line << instruction.name << " instruction [" << instruction.feature;
        for (const auto feature : instruction.required_features) {
            line << ',' << feature;
        }
        line << ']';
        const auto text = std::move(line).str();
        if (emitted.insert(text).second) std::cout << text << '\n';
    }
}

void print_builtins(const CompilerOptions& options) {
    std::cout << "$::expect intrinsic\n$::assume intrinsic\n"
                 "$::unreachable intrinsic\n$::trap intrinsic\n"
                 "$::alignof intrinsic\n$::static_assert intrinsic\n"
                 "$::patch code-generation intrinsic\n"
                 "$::eval translation-time requirement\n"
                 "$::runtime staged-evaluation barrier\n"
                 "$::quote procedural quotation\n"
                 "$::unquote procedural interpolation\n"
                 "$::meta::parse translation intrinsic\n"
                 "$::meta::concat translation intrinsic\n"
                 "$::atomic_load atomic intrinsic\n"
                 "$::atomic_store atomic intrinsic\n"
                 "$::atomic_exchange atomic intrinsic\n"
                 "$::atomic_compare_exchange atomic intrinsic\n"
                 "$::atomic_fetch_add atomic intrinsic\n"
                 "$::atomic_fetch_sub atomic intrinsic\n"
                 "$::atomic_fetch_and atomic intrinsic\n"
                 "$::atomic_fetch_xor atomic intrinsic\n"
                 "$::atomic_fetch_or atomic intrinsic\n"
                 "$::atomic_thread_fence atomic intrinsic\n"
                 "$::atomic_signal_fence atomic intrinsic\n"
                 "$::atomic_is_lock_free atomic query\n"
                 "$::memory::relaxed atomic-order constant\n"
                 "$::memory::acquire atomic-order constant\n"
                 "$::memory::release atomic-order constant\n"
                 "$::memory::acq_rel atomic-order constant\n"
                 "$::memory::seq_cst atomic-order constant\n"
                 "$::has_builtin query\n"
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
                 "$::language::version predefined macro\n"
                 "$::language::version_major predefined macro\n"
                 "$::language::version_minor predefined macro\n"
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
    std::cout << "$::feature::runtime_free_intrinsics\n"
                 "$::feature::control_intrinsics\n"
                  "$::feature::evaluation\n"
                 "$::feature::automatic_evaluation\n"
                 "$::feature::generics\n"
                 "$::feature::procedural_macros\n"
                 "$::feature::patchable_values\n"
                 "$::feature::patchable_operands\n"
                 "$::feature::raw_inline\n";
    std::cout << "$::feature::contextual_attributes\n"
                 "$::feature::external_models\n"
                 "$::feature::operator_binding\n";
    if (const auto* target = target_for_triple(options.target)) {
        if (target->architecture == "x86-64") {
            std::cout << "$::feature::integer128\n"
                         "$::feature::binary128_storage\n"
                         "$::feature::binary128_arithmetic\n"
                         "$::feature::fixed_vectors\n"
                         "$::feature::atomics\n"
                         "$::feature::variadics\n"
                         "$::feature::thread_local\n";
        } else if (target->architecture == "mips" &&
                   resolved_bool(options, "m.llsc")) {
            std::cout << "$::feature::atomics\n";
        }
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

bool preprocess_inputs(const CompilerOptions& options, SourceManager& sources,
                       Diagnostics& diagnostics, std::vector<std::string>& outputs) {
    for (const auto& input : options.inputs) {
        if (input.extension() == ".i") {
            std::string error;
            const auto* source = sources.load(input, error);
            if (!source) diagnostics.command_error(error);
            else outputs.push_back(source->text);
            continue;
        }
        Preprocessor preprocessor(sources, diagnostics, options);
        outputs.push_back(preprocessor.process(input));
    }
    return diagnostics.errors() == 0;
}

} // namespace

int cpp_main(int argc, char** argv) {
    Diagnostics diagnostics(std::cerr);
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
    if (!preprocess_inputs(options, sources, diagnostics, outputs)) return 1;
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
        diagnostics.command_error("target '" + options.target +
                                  "' is not implemented; use --print-targets to list compiled-in targets");
        return 1;
    }
    if (options.emit == EmitKind::Link) {
        diagnostics.command_error("link mode is not implemented yet; use -c or -S");
        return 1;
    }

    SourceManager sources;
    std::vector<std::string> preprocessed;
    if (!preprocess_inputs(options, sources, diagnostics, preprocessed)) return 1;
    if (options.emit == EmitKind::Preprocess) {
        std::ostringstream joined;
        for (const auto& source : preprocessed) joined << source;
        const auto output = options.output.value_or(default_output(options));
        std::string error;
        if (!write_file(output, joined.str(), error)) diagnostics.command_error(error);
        return diagnostics.errors() == 0 ? 0 : 1;
    }

    Program program;
    for (std::size_t i = 0; i < preprocessed.size(); ++i) {
        const auto* source = expand_procedural_macros(
            sources, options.inputs[i], preprocessed[i], diagnostics);
        if (diagnostics.errors() != 0) return 1;
        Lexer lexer(*source, diagnostics);
        Parser parser(lexer.lex(), diagnostics);
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
        for (auto& label : unit.global_labels) {
            program.global_labels.push_back(std::move(label));
        }
        for (auto& function : unit.functions) program.functions.push_back(std::move(function));
        for (auto& object : unit.objects) program.objects.push_back(std::move(object));
    }
    if (diagnostics.errors() != 0) return 1;
    const auto* target = target_for_triple(options.target);
    auto subtarget = resolve_subtarget(*target, options, diagnostics);
    if (!subtarget) return 1;
    program.address_bits = subtarget->abi_info().address_bits;
    if (options.verbose) std::cerr << "cc: expanding generics and compile-time evaluation\n";
    if (!expand_semantics(program, diagnostics, options.evaluate_calls,
                          options.mangling, options.abi)) return 1;
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
    auto raw_assembly = backend->emit_raw_assembly(
        raw_mir, managed_mir, hir_module, *subtarget, options, diagnostics);
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
        auto assembly = backend->emit_managed_assembly(
            managed_mir, hir_module, *subtarget, options, diagnostics);
        assembly += raw_assembly.module_assembly;
        assembly += native::emit_data_assembly(
            codegen_module, *subtarget, options, diagnostics);
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
