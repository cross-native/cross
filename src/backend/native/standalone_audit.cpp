// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/native/standalone_audit.hpp"

#include "common/diagnostic.hpp"
#include "middle/hir.hpp"
#include "middle/machine_ir.hpp"

#include <algorithm>
#include <string>

namespace cross::native {

bool audit_standalone(const machine::Module& machine_module,
                      const hir::Module& hir_module,
                      Diagnostics& diagnostics) {
    bool valid = true;
    for (const auto& function : machine_module.functions) {
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& operand : instruction.operands) {
                    const auto* symbol =
                        std::get_if<machine::SymbolOperand>(&operand);
                    if (!symbol) continue;
                    const bool declared = symbol->is_function
                        ? std::any_of(
                              hir_module.functions.begin(),
                              hir_module.functions.end(),
                              [&](const hir::Function& candidate) {
                                  return candidate.link_symbol == symbol->name;
                              })
                        : std::any_of(
                              hir_module.objects.begin(),
                              hir_module.objects.end(),
                              [&](const hir::Object& candidate) {
                                  return candidate.link_symbol == symbol->name;
                              });
                    if (declared) continue;
                    diagnostics.error(
                        instruction.location,
                        "strictly-standalone native lowering introduced "
                        "undeclared " +
                            std::string(symbol->is_function ? "function '" :
                                                              "object '") +
                            symbol->name + "'");
                    valid = false;
                }
            }
        }
    }
    return valid;
}

} // namespace cross::native
