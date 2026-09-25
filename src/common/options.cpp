// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/options.hpp"

#include <algorithm>
#include <charconv>
#include <iostream>
#include <limits>
#include <string_view>
#include <unordered_map>

namespace cross {
namespace {

bool take_value(int argc, char** argv, int& index, std::string_view option,
                std::string& value, Diagnostics& diagnostics) {
    const std::string_view argument = argv[index];
    if (argument.size() > option.size() &&
        argument.substr(0, option.size()) == option) {
        value = std::string(argument.substr(option.size()));
        return true;
    }
    if (index + 1 >= argc) {
        diagnostics.command_error("missing argument to '" +
                                  std::string(option) + "'");
        return false;
    }
    value = argv[++index];
    return true;
}

std::string command_spelling(std::string_view name) {
    if (name.starts_with("f.")) return "-f" + std::string(name.substr(2));
    if (name.starts_with("m.")) return "-m" + std::string(name.substr(2));
    return std::string(name);
}

std::size_t edit_distance(std::string_view left, std::string_view right) {
    std::vector<std::size_t> row(right.size() + 1);
    for (std::size_t index = 0; index <= right.size(); ++index) {
        row[index] = index;
    }
    for (std::size_t left_index = 0; left_index < left.size(); ++left_index) {
        std::size_t diagonal = row[0];
        row[0] = left_index + 1;
        for (std::size_t right_index = 0;
             right_index < right.size(); ++right_index) {
            const auto previous = row[right_index + 1];
            row[right_index + 1] = std::min(
                {row[right_index + 1] + 1, row[right_index] + 1,
                 diagonal + (left[left_index] == right[right_index] ? 0U
                                                                    : 1U)});
            diagonal = previous;
        }
    }
    return row.back();
}

void add_command_option(CompilerOptions& options, std::string name,
                        OptionValue value, std::string source) {
    options.command_options.push_back(
        {std::move(name), std::move(value), std::move(source)});
}

bool parse_registered_spelling(std::string_view argument,
                               CompilerOptions& options) {
    if (argument.size() <= 2 || argument.front() != '-' ||
        (argument[1] != 'f' && argument[1] != 'm')) {
        return false;
    }
    const char domain = argument[1];
    std::string_view body = argument.substr(2);
    bool value = true;
    if (body.starts_with("no-")) {
        value = false;
        body.remove_prefix(3);
    }
    if (body.empty()) return false;
    const auto equals = body.find('=');
    const auto option_name = body.substr(0, equals);
    if (option_name.empty()) return false;
    const std::string canonical = std::string(1, domain) + "." +
                                  std::string(option_name);
    if (equals == std::string_view::npos) {
        add_command_option(options, canonical, value, std::string(argument));
    } else {
        add_command_option(options, canonical,
                           std::string(body.substr(equals + 1)),
                           std::string(argument));
    }
    return true;
}

bool parse_optimization(std::string_view argument, CompilerOptions& options,
                        Diagnostics& diagnostics) {
    if (argument == "-Ofast") {
        diagnostics.command_error(
            "'-Ofast' is not a Cross preset; use '-O3 -ffast-math'");
        return true;
    }
    if (argument == "-O") {
        options.optimization = "O1";
    } else if (argument == "-O0" || argument == "-Og" ||
               argument == "-O1" || argument == "-O2" ||
               argument == "-O3" || argument == "-Os" ||
               argument == "-Oz") {
        options.optimization = std::string(argument.substr(1));
    } else if (argument.starts_with("-O=")) {
        if (argument.size() == 3) {
            diagnostics.command_error("missing optimization name after '-O='");
            return true;
        }
        options.optimization = std::string(argument.substr(3));
    } else {
        return false;
    }
    options.optimization_explicit = true;
    return true;
}

bool common_option(int argc, char** argv, int& index, CompilerOptions& options,
                   Diagnostics& diagnostics) {
    const std::string_view argument = argv[index];
    std::string value;
    if (argument == "--help") { options.show_help = true; return true; }
    if (argument == "--version") { options.show_version = true; return true; }
    if (argument == "-v") { options.verbose = true; return true; }
    if (argument == "-save-temps") { options.save_temps = true; return true; }
    if (parse_optimization(argument, options, diagnostics)) return true;
    if (argument == "-M" || argument == "-MM" ||
        argument == "-MD" || argument == "-MMD") {
        options.dependency_mode =
            argument == "-M" || argument == "-MM"
                ? DependencyMode::Only : DependencyMode::Alongside;
        return true;
    }
    if (argument == "-MF" || argument.starts_with("-MF")) {
        if (!take_value(argc, argv, index, "-MF", value, diagnostics)) return true;
        options.dependency_file = value;
        return true;
    }
    if (argument == "-MT" || argument.starts_with("-MT") ||
        argument == "-MQ" || argument.starts_with("-MQ")) {
        const std::string_view prefix = argument.starts_with("-MQ") ? "-MQ" : "-MT";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.dependency_targets.push_back({std::move(value),
                                              prefix == std::string_view("-MQ")});
        return true;
    }
    if (argument == "-o" || argument.starts_with("-o")) {
        if (!take_value(argc, argv, index, "-o", value, diagnostics)) return true;
        options.output = value;
        return true;
    }
    if (argument == "-target" || argument.starts_with("-target=")) {
        const auto prefix = argument.starts_with("-target=")
                                ? "-target=" : "-target";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.target = value;
        options.target_explicit = true;
        return true;
    }
    if (argument == "-march" || argument.starts_with("-march=")) {
        const auto prefix = argument.starts_with("-march=")
                                ? "-march=" : "-march";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        add_command_option(options, "m.arch", value,
                           "-march=" + value);
        return true;
    }
    if (argument == "-mtune" || argument.starts_with("-mtune=")) {
        const auto prefix = argument.starts_with("-mtune=")
                                ? "-mtune=" : "-mtune";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        add_command_option(options, "m.tune", value,
                           "-mtune=" + value);
        return true;
    }
    if (argument == "-mabi" || argument.starts_with("-mabi=")) {
        const auto prefix = argument.starts_with("-mabi=")
                                ? "-mabi=" : "-mabi";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.abi = value;
        options.abi_explicit = true;
        return true;
    }
    if (argument == "-mmangling" || argument.starts_with("-mmangling=")) {
        const auto prefix = argument.starts_with("-mmangling=")
                                ? "-mmangling=" : "-mmangling";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.mangling = value;
        options.mangling_explicit = true;
        return true;
    }
    if (argument == "-mprofile" || argument.starts_with("-mprofile=")) {
        const auto prefix = argument.starts_with("-mprofile=")
                                ? "-mprofile=" : "-mprofile";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.profile = value;
        options.profile_explicit = true;
        return true;
    }
    if (argument == "--model-path" ||
        argument.starts_with("--model-path=")) {
        const auto prefix = argument.starts_with("--model-path=")
                                ? "--model-path=" : "--model-path";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.model_paths.emplace_back(value);
        return true;
    }
    if (argument == "--model" || argument.starts_with("--model=")) {
        const auto prefix = argument.starts_with("--model=")
                                ? "--model=" : "--model";
        if (!take_value(argc, argv, index, prefix, value, diagnostics)) return true;
        options.model_files.emplace_back(value);
        return true;
    }
    if (parse_registered_spelling(argument, options)) return true;
    if (argument == "-I" || argument.starts_with("-I")) {
        if (!take_value(argc, argv, index, "-I", value, diagnostics)) return true;
        options.include_paths.emplace_back(value);
        return true;
    }
    if (argument == "-isystem" || argument.starts_with("-isystem")) {
        if (!take_value(argc, argv, index, "-isystem", value, diagnostics)) return true;
        options.system_include_paths.emplace_back(value);
        return true;
    }
    if (argument == "-D" || argument.starts_with("-D")) {
        if (!take_value(argc, argv, index, "-D", value, diagnostics)) return true;
        options.macro_definitions.push_back(value);
        return true;
    }
    if (argument == "-U" || argument.starts_with("-U")) {
        if (!take_value(argc, argv, index, "-U", value, diagnostics)) return true;
        options.macro_undefinitions.push_back(value);
        return true;
    }
    return false;
}

std::optional<OptionValue> coerce_value(const OptionDefinition& definition,
                                        const OptionValue& input,
                                        std::string& reason) {
    switch (definition.kind) {
    case OptionValueKind::Boolean:
        if (const auto* value = std::get_if<bool>(&input)) return *value;
        reason = "expects a boolean -f/-fno or -m/-mno spelling";
        return std::nullopt;
    case OptionValueKind::Unsigned: {
        std::uint64_t value{};
        if (const auto* integer = std::get_if<std::uint64_t>(&input)) {
            value = *integer;
        } else if (const auto* flag = std::get_if<bool>(&input)) {
            const auto* policy =
                std::get_if<std::uint64_t>(&definition.default_value);
            value = *flag ? (policy ? *policy : 1U) : 0U;
        } else if (const auto* text = std::get_if<std::string>(&input)) {
            const auto parsed = std::from_chars(
                text->data(), text->data() + text->size(), value);
            if (text->empty() || parsed.ec != std::errc{} ||
                parsed.ptr != text->data() + text->size()) {
                reason = "expects an unsigned integer value";
                return std::nullopt;
            }
        } else {
            reason = "expects an unsigned integer value";
            return std::nullopt;
        }
        if (value < definition.minimum || value > definition.maximum) {
            reason = "must be between " + std::to_string(definition.minimum) +
                     " and " + std::to_string(definition.maximum);
            return std::nullopt;
        }
        return value;
    }
    case OptionValueKind::Enumeration:
    case OptionValueKind::Text: {
        const auto* value = std::get_if<std::string>(&input);
        if (!value) {
            reason = "expects a value after '='";
            return std::nullopt;
        }
        if (definition.kind == OptionValueKind::Enumeration &&
            std::find(definition.allowed_values.begin(),
                      definition.allowed_values.end(), *value) ==
                definition.allowed_values.end()) {
            reason = "expects one of: ";
            for (std::size_t index = 0;
                 index < definition.allowed_values.size(); ++index) {
                if (index != 0) reason += ", ";
                reason += definition.allowed_values[index];
            }
            return std::nullopt;
        }
        return *value;
    }
    }
    reason = "has an invalid registered type";
    return std::nullopt;
}

} // namespace

std::span<const OptionDefinition> common_option_definitions() {
    static const std::vector<OptionDefinition> definitions{
        {"f.optimize-for", {}, OptionValueKind::Enumeration,
         std::string("debug"), {"debug", "speed", "size", "minimum-size"},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "profitability objective"},
        {"f.ccp-rounds", {}, OptionValueKind::Unsigned,
         std::uint64_t{1}, {}, 1, 16, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "constant-propagation fixed-point rounds"},
        {"f.if-conversion-limit", {}, OptionValueKind::Unsigned,
         std::uint64_t{6}, {}, 0, 1024, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "maximum ordinary MIR cost speculated by if-conversion"},
        {"f.if-conversion-memory-limit", {}, OptionValueKind::Unsigned,
         std::uint64_t{12}, {}, 0, 1024, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "maximum MIR cost speculated for an unpredictable memory condition"},
        {"f.unroll-factor", {}, OptionValueKind::Unsigned,
         std::uint64_t{2}, {}, 1, 16, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "maximum target-independent loop unroll factor"},
        {"f.vector-interleave", {}, OptionValueKind::Unsigned,
         std::uint64_t{2}, {}, 1, 16, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "maximum independent vector reduction streams"},
        {"f.tree-ccp", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "scalar constant propagation and folding"},
        {"f.tree-bit-ccp", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "propagate integer known bits and narrow compatible memory reads"},
        {"f.tree-copy-prop", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "SSA copy and trivial phi propagation"},
        {"f.tree-dce", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "dead SSA value elimination"},
        {"f.tree-dse", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "dead local-slot store elimination"},
        {"f.tree-fre", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "local fully redundant expression elimination"},
        {"f.tree-tail-merge", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "factor profitable common SSA tails before machine selection"},
        {"f.thread-jumps", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "thread branches through proven boolean SSA phis"},
        {"f.tree-cfg-cleanup", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "remove forwarding blocks and repair value/effect SSA"},
        {"f.ivopts", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "coalesce equivalent loop induction variables"},
        {"f.move-loop-invariants", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "hoist proven nontrapping loop-invariant values"},
        {"f.unroll-loops", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "unroll target-independent loops when legality and profitability are proven"},
        {"f.tree-reassoc", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "rebalance legal associative SSA expressions under target cost policy"},
        {"f.tree-slsr", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "replace profitable unsigned constant multiplies with shifts and adds"},
        {"f.tree-loop-rotate", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "rotate legal guarded loops to a bottom continuation test"},
        {"f.tree-loop-vectorize", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "vectorize legal counted loops using target-registered vectors"},
        {"f.tree-early-exit-vectorize", {}, OptionValueKind::Boolean, false,
         {}, 0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "vectorize legal loops with an observable early exit"},
        {"f.tree-slp-vectorize", {}, OptionValueKind::Boolean, false, {}, 0,
         0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "vectorize legal isomorphic straight-line operations"},
        {"f.if-conversion", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "replace profitable control diamonds with data selection"},
        {"f.expensive-optimizations", {}, OptionValueKind::Boolean, false,
         {}, 0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "permit compile-time-intensive profitable machine transforms"},
        {"f.machine-combine", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "combine selected scalar and immediate machine operations"},
        {"f.vector-combine", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "combine and hoist selected vector operations"},
        {"f.machine-cse", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "eliminate redundant pure Machine IR expressions"},
        {"f.machine-load-cse", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "eliminate redundant nonvolatile Machine IR loads"},
        {"f.machine-dce", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "eliminate dead side-effect-free Machine IR definitions"},
        {"f.compare-branch-fusion", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "fuse selected comparisons with conditional branches"},
        {"f.compare-select-fusion", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "fuse selected comparisons with data selections"},
        {"f.jump-tables", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "permit profitable dense machine jump tables"},
        {"f.combine-addresses", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "fold compatible address calculations into memory operations"},
        {"f.fold-memory-operands", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "fold SSA producers and consumers into target memory operands"},
        {"f.schedule-insns", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "schedule instructions within machine basic blocks"},
        {"f.schedule-insns2", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "schedule safe instructions across machine block boundaries"},
        {"f.reorder-blocks", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "lay out machine blocks along profitable control-flow traces"},
        {"f.register-allocation", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "assign eligible virtual values to target registers"},
        {"f.rematerialize", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "rematerialize cheap values instead of retaining spill homes"},
        {"f.optimize-sibling-calls", {}, OptionValueKind::Boolean, false, {},
         0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Partial,
         "replace compatible terminal calls with sibling jumps"},
        {"f.cprop-registers", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "propagate compatible physical-register copies after allocation"},
        {"f.peephole2", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "apply verified target machine peepholes"},
        {"f.inline-functions", {"f.inline"}, OptionValueKind::Boolean, false,
         {}, 0, 0, OptionCategory::Optimization, true,
         OptionImplementation::Implemented, "managed-function inlining"},
        {"f.inline-limit", {}, OptionValueKind::Unsigned, std::uint64_t{96},
         {}, 0, 1048576, OptionCategory::Optimization, true,
         OptionImplementation::Implemented, "MIR inline-size budget"},
        {"f.omit-frame-pointer", {}, OptionValueKind::Boolean, false, {}, 0,
         0, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "permit target frame-pointer elimination"},
        {"f.align-functions", {}, OptionValueKind::Unsigned,
         std::uint64_t{0}, {}, 0, 65536, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "function alignment in bytes; zero selects target policy"},
        {"f.align-loops", {}, OptionValueKind::Unsigned,
         std::uint64_t{16}, {}, 0, 65536, OptionCategory::Optimization, true,
         OptionImplementation::Implemented,
         "hot loop-header alignment in bytes; zero disables padding"},
        {"f.ipa-ra", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "private-call clobber narrowing"},
        {"f.ipa-pure-const", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Partial,
         "managed-function purity and removability inference"},
        {"f.private-abi", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "private parameter and result placement"},
        {"f.ipa-cp-clone", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "constant-argument call specialization"},
        {"f.eval-calls", {}, OptionValueKind::Boolean, true, {}, 0, 0,
         OptionCategory::Semantic, false, OptionImplementation::Implemented,
         "opportunistic translation-time call evaluation"},
        {"f.eval-byte-limit", {}, OptionValueKind::Unsigned,
         std::uint64_t{16 * 1024 * 1024}, {}, 0, 1024 * 1024 * 1024,
         OptionCategory::Semantic, false, OptionImplementation::Implemented,
         "maximum bytes in one translation-time sequence or buffer"},
        {"f.eval-memory-limit", {}, OptionValueKind::Unsigned,
         std::uint64_t{64 * 1024 * 1024}, {}, 0, 1024 * 1024 * 1024,
         OptionCategory::Semantic, false, OptionImplementation::Implemented,
         "cumulative translation-time meta memory per evaluation"},
        {"f.eval-step-limit", {}, OptionValueKind::Unsigned,
         std::uint64_t{1000000}, {}, 1, 100000000,
         OptionCategory::Semantic, false, OptionImplementation::Implemented,
         "maximum evaluator instructions per evaluation"},
        {"f.eval-depth-limit", {}, OptionValueKind::Unsigned,
         std::uint64_t{256}, {}, 1, 512,
         OptionCategory::Semantic, false, OptionImplementation::Implemented,
         "maximum evaluator call depth"},
        {"f.function-sections", {}, OptionValueKind::Boolean, false, {}, 0,
         0, OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented, "one section per function"},
        {"f.data-sections", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented, "one section per data object"},
        {"f.pic", {"f.PIC"}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "position-independent code and data references"},
        {"f.pie", {"f.PIE"}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "position-independent executable code generation"},
        {"f.plt", {}, OptionValueKind::Boolean, true, {}, 0, 0,
         OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "permit ELF procedure-linkage-table calls"},
        {"f.semantic-interposition", {}, OptionValueKind::Boolean, true, {},
         0, 0, OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "preserve ELF interposition of default-visible public definitions"},
        {"f.direct-access-external-data", {}, OptionValueKind::Boolean,
         false, {}, 0, 0, OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "permit direct references to externally visible data"},
        {"f.elide-noreturn-saves", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Optimization, true, OptionImplementation::Implemented,
         "omit unobservable incoming register saves in non-returning functions"},
        {"f.unwind-model", {}, OptionValueKind::Enumeration, std::string("none"),
         {"none", "platform"}, 0, 0, OptionCategory::Semantic, false,
         OptionImplementation::Implemented, "select whether frame unwinding is observable"},
        {"f.unwind-tables", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "emit target unwind records"},
        {"f.asynchronous-unwind-tables", {}, OptionValueKind::Boolean, false,
         {}, 0, 0, OptionCategory::CodeGeneration, false,
         OptionImplementation::Implemented,
         "emit unwind records valid at asynchronous instruction boundaries"},
        {"f.fast-math", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Semantic, false, OptionImplementation::Partial,
         "compound relaxed floating-point policy without hidden calls"},
        {"f.fp-contract", {}, OptionValueKind::Enumeration,
         std::string("off"), {"off", "on", "fast"}, 0, 0,
         OptionCategory::Semantic, false, OptionImplementation::Partial,
         "floating multiply-add contraction policy"},
        {"f.finite-math-only", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Semantic, false, OptionImplementation::Partial,
         "assume floating values are finite"},
        {"f.signed-zeros", {}, OptionValueKind::Boolean, true, {}, 0, 0,
         OptionCategory::Semantic, false, OptionImplementation::Partial,
         "preserve the distinction between positive and negative zero"},
    };
    return definitions;
}

bool resolve_registered_options(
    CompilerOptions& options,
    std::span<const OptionDefinition> target_definitions,
    std::span<const OptionAssignment> preset_options,
    std::span<const OptionAssignment> profile_options,
    Diagnostics& diagnostics) {
    const auto common = common_option_definitions();
    options.resolved_options.clear();

    std::unordered_map<std::string, const OptionDefinition*> canonical;
    std::unordered_map<std::string, const OptionDefinition*> aliases;
    const auto register_definitions = [&](auto definitions) {
        for (const auto& definition : definitions) {
            if (!canonical.emplace(definition.name, &definition).second) {
                diagnostics.command_error(
                    "duplicate compiler option registration '" +
                    definition.name + "'");
                continue;
            }
            for (const auto& alias : definition.aliases) {
                if (!aliases.emplace(alias, &definition).second ||
                    canonical.contains(alias)) {
                    diagnostics.command_error(
                        "duplicate compiler option alias '" + alias + "'");
                }
            }
            options.resolved_options.emplace(
                definition.name,
                ResolvedOption{definition.default_value,
                               OptionOrigin::Default, "default"});
        }
    };
    register_definitions(common);
    register_definitions(target_definitions);
    if (diagnostics.errors() != 0) return false;

    const auto apply = [&](const OptionAssignment& assignment,
                           OptionOrigin origin, bool allow_alias,
                           bool require_presettable) {
        const OptionDefinition* definition{};
        if (const auto found = canonical.find(assignment.name);
            found != canonical.end()) {
            definition = found->second;
        } else if (allow_alias) {
            if (const auto alias_found = aliases.find(assignment.name);
                alias_found != aliases.end()) {
                definition = alias_found->second;
            }
        }
        if (!definition) {
            if (assignment.name == "f.ipa-sra") {
                diagnostics.command_error(
                    "option '-fipa-sra' is reserved for scalar replacement; "
                    "use '-fprivate-abi' for private call placement");
            } else if (assignment.name == "f.builtin" &&
                       assignment.source == "-fno-builtin") {
                diagnostics.command_error(
                    "option '-fno-builtin' has no Cross meaning because Cross "
                    "has no implicit library builtins");
            } else {
                const OptionDefinition* suggestion{};
                std::size_t best = std::numeric_limits<std::size_t>::max();
                for (const auto& [name, candidate] : canonical) {
                    if (name.substr(0, 2) != assignment.name.substr(0, 2)) {
                        continue;
                    }
                    const auto distance = edit_distance(assignment.name, name);
                    if (distance < best) {
                        best = distance;
                        suggestion = candidate;
                    }
                }
                std::string message =
                    "unknown compiler option '" +
                    command_spelling(assignment.name) + "' from " +
                    assignment.source;
                if (suggestion && best <= 3) {
                    message += "; did you mean '" +
                               command_spelling(suggestion->name) + "'?";
                }
                diagnostics.command_error(std::move(message));
            }
            return;
        }
        if (require_presettable && !definition->presettable) {
            diagnostics.command_error(
                "optimization preset cannot set non-presettable option '" +
                definition->name + "' from " + assignment.source);
            return;
        }
        std::string reason;
        auto value = coerce_value(*definition, assignment.value, reason);
        if (!value) {
            diagnostics.command_error(
                "invalid value for option '" +
                command_spelling(definition->name) + "' from " +
                assignment.source + ": " + reason);
            return;
        }
        if (definition->name == "f.align-functions" ||
            definition->name == "f.align-loops") {
            const auto alignment = std::get<std::uint64_t>(*value);
            if (alignment != 0 &&
                (alignment & (alignment - 1U)) != 0) {
                diagnostics.command_error(
                    "invalid value for option '" +
                    command_spelling(definition->name) + "' from " +
                    assignment.source +
                    ": alignment must be zero or a power of two");
                return;
            }
        }
        options.resolved_options[definition->name] =
            ResolvedOption{std::move(*value), origin, assignment.source};
    };

    const auto apply_compound = [&](const OptionAssignment& assignment,
                                    OptionOrigin origin,
                                    bool allow_alias) {
        apply(assignment, origin, allow_alias, false);
        if (assignment.name != "f.fast-math") return;
        const auto found = options.resolved_options.find("f.fast-math");
        if (found == options.resolved_options.end()) return;
        const auto* enabled = std::get_if<bool>(&found->second.value);
        if (!enabled) return;
        apply({"f.finite-math-only", *enabled, assignment.source},
              origin, false, false);
        apply({"f.signed-zeros", !*enabled, assignment.source},
              origin, false, false);
        apply({"f.fp-contract",
               std::string(*enabled ? "fast" : "off"),
               assignment.source},
              origin, false, false);
    };

    for (const auto& setting : preset_options) {
        apply(setting, OptionOrigin::Preset, false, true);
    }
    // A profile is a declarative layer: its explicit granular settings win
    // over a compound setting independently of property order.
    for (const auto& setting : profile_options) {
        if (setting.name == "f.fast-math") {
            apply_compound(setting, OptionOrigin::Profile, false);
        }
    }
    for (const auto& setting : profile_options) {
        if (setting.name != "f.fast-math") {
            apply(setting, OptionOrigin::Profile, false, false);
        }
    }
    // Command-line compounds expand at their written position, so later
    // granular settings override them and a later compound overrides earlier
    // granular settings.
    for (const auto& setting : options.command_options) {
        apply_compound(setting, OptionOrigin::CommandLine, true);
    }
    if (diagnostics.errors() != 0) return false;

    const auto optimize_for =
        resolved_text(options, "f.optimize-for", "debug");
    options.optimize_for = optimize_for == "speed"
        ? OptimizationGoal::Speed
        : optimize_for == "size"
              ? OptimizationGoal::Size
              : optimize_for == "minimum-size"
                    ? OptimizationGoal::MinimumSize
                    : OptimizationGoal::Debug;
    options.ccp_rounds = static_cast<unsigned>(
        resolved_unsigned(options, "f.ccp-rounds", 1));
    options.if_conversion_limit = static_cast<unsigned>(
        resolved_unsigned(options, "f.if-conversion-limit", 6));
    options.if_conversion_memory_limit = static_cast<unsigned>(
        resolved_unsigned(options, "f.if-conversion-memory-limit", 12));
    options.unroll_factor = static_cast<unsigned>(
        resolved_unsigned(options, "f.unroll-factor", 2));
    options.vector_interleave = static_cast<unsigned>(
        resolved_unsigned(options, "f.vector-interleave", 2));
    options.risc_cisc_balance = static_cast<unsigned>(
        resolved_unsigned(options, "m.risc-cisc-balance", 50));
    options.function_alignment = static_cast<unsigned>(
        resolved_unsigned(options, "f.align-functions"));
    options.loop_alignment = static_cast<unsigned>(
        resolved_unsigned(options, "f.align-loops"));
    options.tree_ccp = resolved_bool(options, "f.tree-ccp");
    options.tree_bit_ccp = resolved_bool(options, "f.tree-bit-ccp");
    options.tree_copy_prop =
        resolved_bool(options, "f.tree-copy-prop");
    options.tree_dce = resolved_bool(options, "f.tree-dce");
    options.tree_dse = resolved_bool(options, "f.tree-dse");
    options.tree_fre = resolved_bool(options, "f.tree-fre");
    options.tree_tail_merge =
        resolved_bool(options, "f.tree-tail-merge");
    options.thread_jumps = resolved_bool(options, "f.thread-jumps");
    options.tree_cfg_cleanup =
        resolved_bool(options, "f.tree-cfg-cleanup");
    options.ivopts = resolved_bool(options, "f.ivopts");
    options.move_loop_invariants =
        resolved_bool(options, "f.move-loop-invariants");
    options.unroll_loops = resolved_bool(options, "f.unroll-loops");
    options.tree_reassoc = resolved_bool(options, "f.tree-reassoc");
    options.tree_slsr = resolved_bool(options, "f.tree-slsr");
    options.tree_loop_rotate =
        resolved_bool(options, "f.tree-loop-rotate");
    options.tree_loop_vectorize =
        resolved_bool(options, "f.tree-loop-vectorize");
    options.tree_early_exit_vectorize =
        resolved_bool(options, "f.tree-early-exit-vectorize");
    options.tree_slp_vectorize =
        resolved_bool(options, "f.tree-slp-vectorize");
    options.if_conversion = resolved_bool(options, "f.if-conversion");
    options.expensive_optimizations =
        resolved_bool(options, "f.expensive-optimizations");
    options.machine_combine = resolved_bool(options, "f.machine-combine");
    options.vector_combine = resolved_bool(options, "f.vector-combine");
    options.machine_cse = resolved_bool(options, "f.machine-cse");
    options.machine_load_cse =
        resolved_bool(options, "f.machine-load-cse");
    options.machine_dce = resolved_bool(options, "f.machine-dce");
    options.compare_branch_fusion =
        resolved_bool(options, "f.compare-branch-fusion");
    options.compare_select_fusion =
        resolved_bool(options, "f.compare-select-fusion");
    options.jump_tables = resolved_bool(options, "f.jump-tables");
    options.combine_addresses =
        resolved_bool(options, "f.combine-addresses");
    options.fold_memory_operands =
        resolved_bool(options, "f.fold-memory-operands");
    options.schedule_insns = resolved_bool(options, "f.schedule-insns");
    options.schedule_insns2 = resolved_bool(options, "f.schedule-insns2");
    options.reorder_blocks = resolved_bool(options, "f.reorder-blocks");
    options.register_allocation =
        resolved_bool(options, "f.register-allocation");
    options.rematerialize = resolved_bool(options, "f.rematerialize");
    options.optimize_sibling_calls =
        resolved_bool(options, "f.optimize-sibling-calls");
    options.cprop_registers =
        resolved_bool(options, "f.cprop-registers");
    options.peephole2 = resolved_bool(options, "f.peephole2");
    options.inline_functions =
        resolved_bool(options, "f.inline-functions");
    options.inline_unit_limit = static_cast<unsigned>(
        resolved_unsigned(options, "f.inline-limit", 96));
    options.omit_frame_pointer =
        resolved_bool(options, "f.omit-frame-pointer");
    options.ipa_ra = resolved_bool(options, "f.ipa-ra");
    options.ipa_pure_const =
        resolved_bool(options, "f.ipa-pure-const");
    options.private_abi = resolved_bool(options, "f.private-abi");
    options.ipa_cp_clone = resolved_bool(options, "f.ipa-cp-clone");
    options.evaluate_calls = resolved_bool(options, "f.eval-calls", true);
    options.eval_byte_limit = resolved_unsigned(options, "f.eval-byte-limit", 16 * 1024 * 1024);
    options.eval_memory_limit = resolved_unsigned(options, "f.eval-memory-limit", 64 * 1024 * 1024);
    options.eval_step_limit = resolved_unsigned(options, "f.eval-step-limit", 1000000);
    options.eval_depth_limit = static_cast<unsigned>(resolved_unsigned(options, "f.eval-depth-limit", 256));
    options.function_sections =
        resolved_bool(options, "f.function-sections");
    options.data_sections = resolved_bool(options, "f.data-sections");
    options.pie = resolved_bool(options, "f.pie");
    options.position_independent = resolved_bool(options, "f.pic") ||
                                   options.pie;
    options.use_plt = resolved_bool(options, "f.plt", true);
    options.semantic_interposition =
        resolved_bool(options, "f.semantic-interposition", true);
    options.direct_external_data =
        resolved_bool(options, "f.direct-access-external-data");
    options.unwind_tables =
        resolved_bool(options, "f.unwind-tables");
    options.asynchronous_unwind_tables =
        resolved_bool(options, "f.asynchronous-unwind-tables");
    options.elide_noreturn_saves = resolved_bool(options, "f.elide-noreturn-saves");
    options.unwind_model = resolved_text(options, "f.unwind-model") == "platform"
        ? UnwindModel::Platform : UnwindModel::None;
    const auto code_model = resolved_text(options, "m.cmodel", "small");
    options.code_model = code_model == "kernel"
        ? CodeModel::Kernel
        : code_model == "medium"
              ? CodeModel::Medium
              : code_model == "large" ? CodeModel::Large
                                        : CodeModel::Small;
    options.fast_math = resolved_bool(options, "f.fast-math");
    const auto fp_contract =
        resolved_text(options, "f.fp-contract", "off");
    options.fp_contract = fp_contract == "fast"
        ? FpContractMode::Fast
        : fp_contract == "on" ? FpContractMode::On
                               : FpContractMode::Off;
    options.finite_math_only =
        resolved_bool(options, "f.finite-math-only");
    options.signed_zeros = resolved_bool(options, "f.signed-zeros", true);
    return true;
}

const ResolvedOption* find_resolved_option(const CompilerOptions& options,
                                           std::string_view name) {
    const auto found = options.resolved_options.find(std::string(name));
    return found == options.resolved_options.end() ? nullptr : &found->second;
}

bool resolved_bool(const CompilerOptions& options, std::string_view name,
                   bool fallback) {
    const auto* option = find_resolved_option(options, name);
    if (!option) return fallback;
    const auto* value = std::get_if<bool>(&option->value);
    return value ? *value : fallback;
}

std::uint64_t resolved_unsigned(const CompilerOptions& options,
                                std::string_view name,
                                std::uint64_t fallback) {
    const auto* option = find_resolved_option(options, name);
    if (!option) return fallback;
    const auto* value = std::get_if<std::uint64_t>(&option->value);
    return value ? *value : fallback;
}

std::string_view resolved_text(const CompilerOptions& options,
                               std::string_view name,
                               std::string_view fallback) {
    const auto* option = find_resolved_option(options, name);
    if (!option) return fallback;
    const auto* value = std::get_if<std::string>(&option->value);
    return value ? std::string_view(*value) : fallback;
}

std::string_view option_origin_name(OptionOrigin origin) {
    switch (origin) {
    case OptionOrigin::Default: return "default";
    case OptionOrigin::Preset: return "preset";
    case OptionOrigin::Profile: return "profile";
    case OptionOrigin::CommandLine: return "command-line";
    }
    return "unknown";
}

std::string option_value_text(const OptionValue& value) {
    if (const auto* boolean = std::get_if<bool>(&value)) {
        return *boolean ? "on" : "off";
    }
    if (const auto* integer = std::get_if<std::uint64_t>(&value)) {
        return std::to_string(*integer);
    }
    return std::get<std::string>(value);
}

std::string option_type_text(const OptionDefinition& definition) {
    switch (definition.kind) {
    case OptionValueKind::Boolean: return "boolean";
    case OptionValueKind::Unsigned:
        return "unsigned[" + std::to_string(definition.minimum) + ".." +
               std::to_string(definition.maximum) + "]";
    case OptionValueKind::Enumeration: {
        std::string result{"enum{"};
        for (std::size_t index = 0;
             index < definition.allowed_values.size(); ++index) {
            if (index != 0) result += '|';
            result += definition.allowed_values[index];
        }
        return result + '}';
    }
    case OptionValueKind::Text: return "text";
    }
    return "unknown";
}

bool parse_cc_options(int argc, char** argv, CompilerOptions& options,
                      Diagnostics& diagnostics) {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "-S") { options.emit = EmitKind::Assembly; continue; }
        if (argument == "-c") { options.emit = EmitKind::Object; continue; }
        if (argument == "-E") { options.emit = EmitKind::Preprocess; continue; }
        if (argument == "-emit-llvm") {
            options.emit = EmitKind::LlvmTextDebug;
            continue;
        }
        if (argument == "-emit-gimple") {
            options.emit = EmitKind::GimpleTextDebug;
            continue;
        }
        if (argument == "-emit-gimple=rtl") {
            options.emit = EmitKind::GimpleRtlTextDebug;
            continue;
        }
        if (argument == "--print-targets") { options.print_targets = true; continue; }
        if (argument == "--print-abis" || argument == "-mabi=help") { options.print_abis = true; continue; }
        if (argument == "--print-models") { options.print_models = true; continue; }
        if (argument == "--print-profiles") { options.print_profiles = true; continue; }
        if (argument == "--print-manglings") { options.print_manglings = true; continue; }
        if (argument == "--print-optimizations") { options.print_optimizations = true; continue; }
        if (argument == "--print-options" ||
            argument.starts_with("--print-options=")) {
            options.print_options = true;
            if (argument.starts_with("--print-options=")) {
                options.print_options_category =
                    std::string(argument.substr(16));
                if (options.print_options_category.empty()) {
                    diagnostics.command_error(
                        "missing category after '--print-options='");
                } else if (options.print_options_category != "all" &&
                           options.print_options_category != "optimization" &&
                           options.print_options_category != "common" &&
                           options.print_options_category != "target") {
                    diagnostics.command_error(
                        "unknown --print-options category '" +
                        options.print_options_category +
                        "'; expected all, optimization, common, or target");
                }
            }
            continue;
        }
        if (argument == "--print-attributes") { options.print_attributes = true; continue; }
        if (argument == "--print-registers") { options.print_registers = true; continue; }
        if (argument == "--print-keywords") { options.print_keywords = true; continue; }
        if (argument == "--print-builtins") { options.print_builtins = true; continue; }
        if (argument == "--print-instructions") { options.print_instructions = true; continue; }
        if (argument == "--print-features") { options.print_features = true; continue; }
        if (common_option(argc, argv, index, options, diagnostics)) continue;
        if (!argument.empty() && argument.front() == '-') {
            diagnostics.command_error("unknown argument '" +
                                      std::string(argument) + "'");
            continue;
        }
        options.inputs.emplace_back(argument);
    }
    if (options.dependency_mode == DependencyMode::None &&
        (options.dependency_file || !options.dependency_targets.empty()))
        diagnostics.command_error("-MF, -MT, and -MQ require -M, -MM, -MD, or -MMD");
    return diagnostics.errors() == 0;
}

bool parse_cpp_options(int argc, char** argv, CompilerOptions& options,
                       Diagnostics& diagnostics) {
    options.emit = EmitKind::Preprocess;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "-E") continue;
        if (common_option(argc, argv, index, options, diagnostics)) continue;
        if (!argument.empty() && argument.front() == '-') {
            diagnostics.command_error("unknown argument '" +
                                      std::string(argument) + "'");
            continue;
        }
        options.inputs.emplace_back(argument);
    }
    if (options.dependency_mode == DependencyMode::None &&
        (options.dependency_file || !options.dependency_targets.empty()))
        diagnostics.command_error("-MF, -MT, and -MQ require -M, -MM, -MD, or -MMD");
    return diagnostics.errors() == 0;
}

void print_version() {
    std::cout << "Cross toolchain 0.1.0 (language 0.8)\n"
                 "Copyright (C) 2026 Cross contributors\n"
                 "License GPLv3+: GNU GPL version 3 or later\n";
}

void print_cc_help() {
    std::cout << R"(Usage: cc [options] input.x ...
  -E                    preprocess only
  -S                    emit target assembly
  -c                    emit object code
  -emit-llvm            serialize optional debug/compatibility LLVM IR
  -emit-gimple          serialize GCC GIMPLE SSA for GIMPLE + RTL passes
  -emit-gimple=rtl      serialize optimized GIMPLE SSA for GCC RTL passes
  -o FILE               write output to FILE
  -I DIR/-isystem DIR   add ordered source search directories
  -M/-MM                emit Make dependencies instead of source
  -MD/-MMD              emit source and a Make dependency file
  -MF FILE              set dependency output path
  -MT TARGET/-MQ TARGET set dependency target (raw/Make-escaped)
  -O0/-Og/-O1/-O2/-O3/-Os/-Oz
                        select a model-defined optimization preset
  -O=NAME               select any loaded optimization preset
  -fOPTION/-fno-OPTION  set a registered common boolean option
  -fOPTION=VALUE        set a registered common valued option
  -target TRIPLE        select a compiled-in target
  -march=CPU            select target instruction compatibility
  -mtune=CPU            select target scheduling and cost preferences
  -mFEATURE/-mno-FEATURE
                        enable or disable a registered target feature
  -mOPTION=VALUE        set a registered target option
  -mabi=NAME            select a registered target ABI
  -mmangling=NAME       select a registered name-mangling model
  -mprofile=NAME        select a model profile
  --model=FILE          load a named compiler-definition file
  --model-path=DIR      add a compiler-definition search directory
  --print-targets       list compiled-in target architectures
  --print-abis          list target ABI entries
  --print-models        list loaded model sources
  --print-profiles      list model profiles
  --print-manglings     list name-mangling models
  --print-optimizations list loaded optimization presets
  --print-options[=CATEGORY]
                        list typed effective options and origins
  --print-attributes    list contextual [[attribute]] names
  --print-registers     list target register names, storage, and widths
  --print-keywords      list every reserved Cross keyword
  --print-builtins      list implemented $:: names and categories
  --print-instructions  list instruction built-ins
  --print-features      list feature queries
)";
}

void print_cpp_help() {
    std::cout << R"(Usage: cpp [options] input.x ...
  -Dname[=value]        define a macro
  -Uname                undefine a macro
  -Ipath                add an include directory
  -isystem DIR          add a later-searched include directory
  -M/-MM                emit Make dependencies instead of source
  -MD/-MMD              emit source and a Make dependency file
  -MF FILE              set dependency output path
  -MT TARGET/-MQ TARGET set dependency target (raw/Make-escaped)
  -target TRIPLE        select target macros
  -march=CPU            select target instruction compatibility
  -mtune=CPU            select target tuning
  -mOPTION/-mno-OPTION  set a registered target option
  -mabi=NAME            select target ABI macros
  -mmangling=NAME       select a name-mangling model
  -mprofile=NAME        select a model profile
  --model=FILE          load a named compiler-definition file
  --model-path=DIR      add a compiler-definition search directory
  -o FILE               write canonical Cross to FILE
)";
}

} // namespace cross
