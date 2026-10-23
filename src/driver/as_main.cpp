// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/assembler.hpp"
#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "common/source.hpp"
#include "model/model.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

namespace {

using namespace cross;

// Applies -EB/-EL and -32/-64 to the architecture of a MIPS triple.
bool apply_gnu_selection(CompilerOptions& options, const AssemblerCommand& command,
                         Diagnostics& diagnostics) {
    if (!command.big_endian && !command.address_bits) return true;
    const auto dash = options.target.find('-');
    std::string architecture = options.target.substr(0, dash);
    const std::string rest = dash == std::string::npos ? "" : options.target.substr(dash);
    if (!architecture.starts_with("mips")) {
        diagnostics.command_error("-EB, -EL, -32, and -64 select MIPS variants; target '" +
                                  options.target + "' is not MIPS");
        return false;
    }
    bool little = architecture.ends_with("el");
    if (little) architecture.resize(architecture.size() - 2);
    if (command.big_endian) little = !*command.big_endian;
    if (command.address_bits) {
        const bool wide = *command.address_bits == 64;
        if (architecture == "mips" && wide) architecture = "mips64";
        else if (architecture == "mips64" && !wide) architecture = "mips";
        else if (architecture != "mips" && architecture != "mips64") {
            diagnostics.command_error("-32 and -64 do not apply to target '" + options.target + "'");
            return false;
        }
        options.abi = wide ? "n64" : "o32";
        options.abi_explicit = true;
    }
    options.target = architecture + (little ? "el" : "") + rest;
    return true;
}

int assemble_main(int argc, char** argv) {
    Diagnostics diagnostics(std::cerr, "as");
    CompilerOptions options;
    AssemblerCommand command;
    if (!parse_as_options(argc, argv, options, command, diagnostics)) return 1;
    if (options.show_help) {
        print_as_help();
        return 0;
    }
    if (options.show_version) {
        print_version();
        return 0;
    }
    // An explicit triple changes before the models pick its profile; a
    // profile's triple changes after.
    if (options.target_explicit && !apply_gnu_selection(options, command, diagnostics)) return 1;
    if (!configure_models(options, diagnostics)) return 1;
    if (!options.target_explicit && !apply_gnu_selection(options, command, diagnostics)) return 1;
    if (options.inputs.empty()) {
        diagnostics.command_error("no input files");
        return 1;
    }
    const auto* target = target_for_triple(options.target);
    if (!target) {
        diagnostics.command_error("target '" + options.target +
                                  "' is not implemented; use -target or -mprofile");
        return 1;
    }
    if (!target->integrated_assembler) {
        diagnostics.command_error("target '" + options.target + "' has no assembler");
        return 1;
    }
    auto subtarget = resolve_subtarget(*target, options, diagnostics);
    if (!subtarget) return 1;

    SourceManager manager;
    std::vector<const SourceFile*> sources;
    for (const auto& input : options.inputs) {
        if (input == "-") {
            std::string text((std::istreambuf_iterator<char>(std::cin)),
                             std::istreambuf_iterator<char>());
            sources.push_back(manager.add("<stdin>", std::move(text)));
            continue;
        }
        std::string error;
        const auto* source = manager.load(input, error);
        if (!source) {
            diagnostics.command_error(error);
            return 1;
        }
        sources.push_back(source);
    }
    assembler::Options assembler_options;
    assembler_options.include_paths = options.include_paths;
    assembler_options.fatal_warnings = command.fatal_warnings;
    assembler_options.no_warnings = command.no_warnings;
    assembler_options.optimize = command.optimize;
    assembler_options.directives = command.directives;
    if (!options.inputs.empty() && options.inputs.front() != "-") {
        assembler_options.source_directory = options.inputs.front().parent_path();
    }
    const auto object = target->integrated_assembler(sources, *subtarget, assembler_options,
                                                     manager, diagnostics);
    if (!object || diagnostics.errors() != 0) return 1;
    const auto output = options.output.value_or("a.out");
    std::ofstream file(output, std::ios::binary);
    file.write(reinterpret_cast<const char*>(object->data()),
               static_cast<std::streamsize>(object->size()));
    if (!file) {
        diagnostics.command_error("cannot write '" + output.string() + "'");
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) { return assemble_main(argc, argv); }
