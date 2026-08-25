// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/options.hpp"
#include "model/model.hpp"
#include "target/subtarget.hpp"
#include "target/x86_64/target.hpp"

#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::optional<cross::CompilerOptions> parse(
    std::vector<std::string> arguments, std::string* error = nullptr) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (auto& argument : arguments) argv.push_back(argument.data());
    std::ostringstream errors;
    cross::Diagnostics diagnostics(errors);
    cross::CompilerOptions options;
    const bool valid = cross::parse_cc_options(
                           static_cast<int>(argv.size()), argv.data(),
                           options, diagnostics) &&
                       cross::configure_models(options, diagnostics);
    if (error) *error = errors.str();
    if (!valid) return std::nullopt;
    return options;
}

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << '\n';
    return false;
}

} // namespace

int main() {
    bool ok = true;
    const auto gimple = parse({"cc", "-emit-gimple"});
    const auto gimple_rtl = parse({"cc", "-emit-gimple=rtl"});
    ok = expect(gimple &&
                    gimple->emit == cross::EmitKind::GimpleTextDebug &&
                    gimple_rtl &&
                    gimple_rtl->emit == cross::EmitKind::GimpleRtlTextDebug,
                "GIMPLE output modes did not parse") &&
         ok;

    const auto o0 = parse({"cc", "-O0"});
    ok = expect(o0 && !o0->tree_ccp && !o0->tree_copy_prop &&
                    !o0->tree_dce && !o0->tree_dse && !o0->tree_fre &&
                    !o0->inline_functions && !o0->ipa_ra &&
                    !o0->ipa_pure_const &&
                    !o0->private_abi && !o0->ipa_cp_clone &&
                    !o0->omit_frame_pointer &&
                    !o0->fast_math && !o0->finite_math_only &&
                    o0->signed_zeros &&
                    o0->inline_unit_limit == 96,
                "O0 does not match its model-defined baseline") &&
         ok;

    const auto og = parse({"cc", "-Og"});
    ok = expect(og && og->tree_ccp && !og->tree_dce &&
                    !og->inline_functions &&
                    og->optimization_effort == 1,
                "Og preset does not preserve debuggable policy") &&
         ok;

    const auto o2 = parse({"cc", "-O2"});
    ok = expect(o2 && o2->tree_ccp && o2->tree_copy_prop &&
                    o2->tree_dce && o2->tree_dse && o2->tree_fre &&
                    o2->inline_functions && o2->ipa_ra &&
                    o2->ipa_pure_const &&
                    o2->private_abi && !o2->ipa_cp_clone &&
                    o2->inline_unit_limit == 96 &&
                    o2->optimization_effort == 2,
                "O2 preset does not resolve to the model-defined passes") &&
         ok;

    const auto o3_override = parse(
        {"cc", "-fno-inline", "-fno-tree-dce", "-O3",
         "-fno-tree-dse", "-fno-ipa-ra", "-fno-ipa-pure-const"});
    ok = expect(o3_override && o3_override->tree_ccp &&
                    !o3_override->tree_dce &&
                    !o3_override->tree_dse &&
                    !o3_override->inline_functions &&
                    !o3_override->ipa_ra &&
                    !o3_override->ipa_pure_const &&
                    o3_override->private_abi &&
                    o3_override->ipa_cp_clone,
                "explicit negative pass options did not override O3") &&
         ok;

    const auto reverse_order = parse(
        {"cc", "-O3", "-fno-inline-functions",
         "-fno-tree-dce", "-fno-tree-dse", "-fno-ipa-ra",
         "-fno-ipa-pure-const"});
    ok = expect(
             reverse_order && o3_override &&
                 reverse_order->tree_ccp == o3_override->tree_ccp &&
                 reverse_order->tree_dce == o3_override->tree_dce &&
                 reverse_order->tree_dse == o3_override->tree_dse &&
                 reverse_order->inline_functions ==
                     o3_override->inline_functions &&
                 reverse_order->ipa_ra == o3_override->ipa_ra &&
                 reverse_order->ipa_pure_const ==
                     o3_override->ipa_pure_const &&
                 reverse_order->private_abi == o3_override->private_abi &&
                 reverse_order->ipa_cp_clone ==
                     o3_override->ipa_cp_clone,
             "direct pass overrides depend on their position relative to O3") &&
         ok;

    const auto explicit_o0 = parse(
        {"cc", "-O0", "-finline-functions", "-ftree-ccp",
         "-finline-limit=17"});
    ok = expect(explicit_o0 && explicit_o0->inline_functions &&
                    explicit_o0->tree_ccp &&
                    explicit_o0->inline_unit_limit == 17,
                "direct optimization controls are gated by O0") &&
         ok;

    const auto fast_then_granular = parse(
        {"cc", "-ffast-math", "-fno-finite-math-only",
         "-fsigned-zeros"});
    ok = expect(fast_then_granular && fast_then_granular->fast_math &&
                    !fast_then_granular->finite_math_only &&
                    fast_then_granular->signed_zeros &&
                    fast_then_granular->fp_contract ==
                        cross::FpContractMode::Fast,
                "granular floating controls did not override fast-math") &&
         ok;

    const auto granular_then_fast = parse(
        {"cc", "-fno-finite-math-only", "-fsigned-zeros",
         "-ffast-math"});
    ok = expect(granular_then_fast && granular_then_fast->fast_math &&
                    granular_then_fast->finite_math_only &&
                    !granular_then_fast->signed_zeros &&
                    granular_then_fast->fp_contract ==
                        cross::FpContractMode::Fast,
                "fast-math did not expand at its command-line position") &&
         ok;

    const auto strict_again = parse({"cc", "-ffast-math",
                                      "-fno-fast-math"});
    ok = expect(strict_again && !strict_again->fast_math &&
                    !strict_again->finite_math_only &&
                    strict_again->signed_zeros &&
                    strict_again->fp_contract ==
                        cross::FpContractMode::Off,
                "negative fast-math did not restore granular defaults") &&
         ok;

    const auto explicit_contract = parse(
        {"cc", "-ffast-math", "-ffp-contract=off"});
    ok = expect(explicit_contract && explicit_contract->fast_math &&
                    explicit_contract->fp_contract ==
                        cross::FpContractMode::Off,
                "explicit contraction policy did not override fast-math") &&
         ok;

    const auto tuned = parse(
        {"cc", "-march=x86-64", "-mtune=znver3", "-mavx512f"});
    ok = expect(tuned && tuned->cpu == "x86-64" &&
                    tuned->tune == "znver3" &&
                    cross::resolved_bool(*tuned, "m.avx") &&
                    cross::resolved_bool(*tuned, "m.avx2") &&
                    cross::resolved_bool(*tuned, "m.avx512f"),
                "target option resolution did not separate arch and tune") &&
         ok;

    const auto target_policies = parse(
        {"cc", "-target", "x86_64-unknown-linux-gnu", "-mred-zone",
         "-mprefer-vector-width=256"});
    ok = expect(target_policies &&
                    cross::resolved_bool(*target_policies, "m.red-zone") &&
                    cross::resolved_text(
                        *target_policies, "m.prefer-vector-width") == "256",
                "x86-64 stack/vector target policies did not resolve") &&
         ok;

    const auto full_vector = parse({"cc", "-mavx512bw"});
    ok = expect(full_vector &&
                    cross::resolved_bool(*full_vector, "m.avx") &&
                    cross::resolved_bool(*full_vector, "m.avx2") &&
                    cross::resolved_bool(*full_vector, "m.f16c") &&
                    cross::resolved_bool(*full_vector, "m.fma") &&
                    cross::resolved_bool(*full_vector, "m.avx512f") &&
                    cross::resolved_bool(*full_vector, "m.avx512bw"),
                "target feature prerequisites were not expanded") &&
         ok;

    const auto& x86_target = cross::x86_64_target();
    const auto* feature_table = cross::subtarget_table_for(x86_target);
    bool feature_ids_match = feature_table &&
        feature_table->features.size() == static_cast<std::size_t>(
            cross::x86_64::Feature::Count);
    if (feature_ids_match) {
        for (std::size_t index = 0; index < feature_table->features.size();
             ++index) {
            if (feature_table->features[index].name !=
                cross::x86_64::feature_name(
                    static_cast<cross::x86_64::Feature>(index))) {
                feature_ids_match = false;
                break;
            }
        }
    }
    ok = expect(feature_ids_match,
                "typed x86 feature IDs diverged from the target table") &&
         ok;

    std::ostringstream subtarget_errors;
    cross::Diagnostics subtarget_diagnostics(subtarget_errors);
    const auto selected = full_vector
        ? cross::resolve_subtarget(
              x86_target, *full_vector, subtarget_diagnostics)
        : std::nullopt;
    ok = expect(selected &&
                    selected->has_feature(cross::x86_64::Feature::Avx) &&
                    selected->has_feature(cross::x86_64::Feature::Avx2) &&
                    selected->has_feature(
                        cross::x86_64::Feature::Avx512bw) &&
                    selected->feature_name(
                        cross::x86_64::Feature::Avx2) == "avx2",
                "typed target-feature queries did not match text resolution") &&
         ok;

    const auto architecture_level = parse({"cc", "-march=x86-64-v4"});
    ok = expect(architecture_level &&
                    architecture_level->cpu == "x86-64-v4" &&
                    cross::resolved_bool(*architecture_level, "m.avx2") &&
                    cross::resolved_bool(*architecture_level, "m.avx512f") &&
                    cross::resolved_bool(*architecture_level, "m.avx512dq") &&
                    cross::resolved_bool(*architecture_level, "m.avx512cd") &&
                    cross::resolved_bool(*architecture_level, "m.avx512bw") &&
                    cross::resolved_bool(*architecture_level, "m.avx512vl"),
                "x86-64-v4 architecture level did not expand its ISA set") &&
         ok;

    const auto disabled_baseline =
        parse({"cc", "-march=haswell", "-mno-avx"});
    ok = expect(disabled_baseline &&
                    !cross::resolved_bool(*disabled_baseline, "m.avx") &&
                    !cross::resolved_bool(*disabled_baseline, "m.avx2") &&
                    !cross::resolved_bool(*disabled_baseline, "m.f16c") &&
                    !cross::resolved_bool(*disabled_baseline, "m.fma") &&
                    cross::resolved_bool(*disabled_baseline, "m.bmi2"),
                "disabling a prerequisite did not disable CPU dependents") &&
         ok;

    const auto disabled_transitive_baseline =
        parse({"cc", "-march=haswell", "-mno-sse4.2"});
    ok = expect(disabled_transitive_baseline &&
                    !cross::resolved_bool(
                        *disabled_transitive_baseline, "m.sse4.2") &&
                    !cross::resolved_bool(
                        *disabled_transitive_baseline, "m.avx") &&
                    !cross::resolved_bool(
                        *disabled_transitive_baseline, "m.f16c") &&
                    !cross::resolved_bool(
                        *disabled_transitive_baseline, "m.fma") &&
                    !cross::resolved_bool(
                        *disabled_transitive_baseline, "m.avx2") &&
                    !cross::resolved_bool(
                        *disabled_transitive_baseline, "m.avx512f"),
                "transitively disabling a CPU prerequisite did not converge") &&
         ok;

    std::string transitive_conflict_error;
    const auto transitive_conflict = parse(
        {"cc", "-mavx2", "-mno-sse4.2"},
        &transitive_conflict_error);
    ok = expect(!transitive_conflict &&
                    transitive_conflict_error.find(
                        "requires '-msse4.2'") != std::string::npos,
                "transitive target-feature contradiction lacks a diagnostic") &&
         ok;

    const auto avx512_lengths = parse({"cc", "-mavx512vl"});
    ok = expect(avx512_lengths &&
                    cross::resolved_bool(*avx512_lengths, "m.avx") &&
                    cross::resolved_bool(*avx512_lengths, "m.avx2") &&
                    cross::resolved_bool(*avx512_lengths, "m.avx512f") &&
                    cross::resolved_bool(*avx512_lengths, "m.avx512vl") &&
                    !cross::resolved_bool(*avx512_lengths, "m.avx512bw"),
                "independent AVX-512 subset prerequisites were not resolved") &&
         ok;

    std::string feature_conflict_error;
    const auto feature_conflict = parse(
        {"cc", "-mavx2", "-mno-avx"}, &feature_conflict_error);
    ok = expect(!feature_conflict &&
                    feature_conflict_error.find(
                        "requires '-mavx'") != std::string::npos,
                "contradictory target extensions lack a dependency diagnostic") &&
         ok;

    std::string fma_conflict_error;
    const auto fma_conflict = parse(
        {"cc", "-mfma", "-mno-avx"}, &fma_conflict_error);
    ok = expect(!fma_conflict &&
                    fma_conflict_error.find(
                        "requires '-mavx'") != std::string::npos,
                "FMA prerequisite contradiction lacks a diagnostic") &&
         ok;

    std::string invalid_error;
    const auto invalid = parse({"cc", "-mnot-a-feature"}, &invalid_error);
    ok = expect(!invalid &&
                    invalid_error.find("unknown compiler option") !=
                        std::string::npos,
                "unknown target option was silently accepted") &&
         ok;

    std::string retired_error;
    const auto retired = parse({"cc", "-fipa-sra"}, &retired_error);
    ok = expect(!retired &&
                    retired_error.find("use '-fprivate-abi'") !=
                        std::string::npos,
                "retired private-ABI spelling lacks a migration diagnostic") &&
         ok;

    std::string suggestion_error;
    const auto typo = parse({"cc", "-ftree-dcee"}, &suggestion_error);
    ok = expect(!typo &&
                    suggestion_error.find("did you mean '-ftree-dce'") !=
                        std::string::npos,
                "unknown optimization option lacks a useful suggestion") &&
         ok;

    std::string alignment_error;
    const auto bad_alignment =
        parse({"cc", "-falign-functions=24"}, &alignment_error);
    ok = expect(!bad_alignment &&
                    alignment_error.find("power of two") != std::string::npos,
                "invalid target alignment was silently rounded") &&
         ok;

    std::string builtin_error;
    const auto builtin = parse({"cc", "-fno-builtin"}, &builtin_error);
    ok = expect(!builtin &&
                    builtin_error.find("no implicit library builtins") !=
                        std::string::npos,
                "runtime compatibility option lacks a standalone diagnostic") &&
         ok;
    return ok ? 0 : 1;
}
