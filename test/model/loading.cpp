// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "model/model.hpp"

#include <algorithm>
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    const std::string_view expected = argv[1];
    cross::Diagnostics diagnostics(std::cerr);
    cross::CompilerOptions options;
    if (!cross::parse_cc_options(argc - 1, argv + 1, options, diagnostics) ||
        !cross::configure_models(options, diagnostics)) return 1;
    const auto& registry = cross::model_registry();
    for (const auto* target : cross::all_targets()) {
        const bool loaded = expected == "all" || expected == target->architecture;
        if ((registry.find_cpu(target->architecture, "generic") != nullptr) != loaded ||
            std::any_of(registry.abis().begin(), registry.abis().end(),
                        [&](const auto& abi) { return abi.architecture == target->architecture; }) != loaded ||
            (target->instruction_model &&
             ((registry.instruction_table(target->architecture) != nullptr) != loaded))) {
            std::cerr << "unexpected loaded architecture: " << target->architecture << '\n';
            return 1;
        }
    }
    for (const auto& entry : registry.cpus()) {
        if (expected != "all" && entry.architecture != expected) return 1;
    }
    for (const auto& entry : registry.abis()) {
        if (expected != "all" && entry.architecture != expected) return 1;
    }
    if (!registry.find_profile("switch") || !registry.find_profile("vr4300-o32") ||
        !registry.find_profile("x86_64-elf") || !registry.find_mangling("itanium")) return 1;

    // Reconfiguration can add an architecture without duplicating entries or
    // changing the identity of a previously resolved ABI.
    const auto old_abi = registry.abis().front().id;
    const auto old_name = registry.abis().front().canonical_name;
    options.print_targets = true;
    if (!cross::configure_models(options, diagnostics) ||
        !registry.find_cpu("mips", "generic") ||
        !registry.find_cpu("aarch64", "generic") ||
        registry.find_abi(old_abi)->canonical_name != old_name) return 1;
    return 0;
}
