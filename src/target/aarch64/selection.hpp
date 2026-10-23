// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "middle/machine_ir.hpp"
#include "middle/mir.hpp"
#include "target/subtarget.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cross::aarch64 {

// A function selected into Machine IR over virtual registers, with the
// facts that frame lowering needs and Machine IR does not hold.
struct SelectedFunction {
    machine::Function function;
    // Each incoming stack argument slot and its offset from the caller's sp
    // at the call.
    std::vector<std::pair<machine::StackSlotId, std::uint32_t>> incoming_arguments;
    // The area of outgoing stack arguments at sp, if a call has any.
    std::optional<machine::StackSlotId> outgoing_arguments;
    // Whether a return is reachable from the entry.
    bool returns{true};
    // The registers that return the function's result, which its frame
    // does not restore.
    std::vector<std::uint32_t> result_registers;
};

// Selects managed MIR into AArch64 Machine IR. Interface values move
// through explicit copies to and from the physical registers the ABI model
// places them in; every other value is a virtual register.
[[nodiscard]] std::vector<SelectedFunction> select_module(
    const mir::ManagedModule& managed, const hir::Module& hir_module,
    const Subtarget& subtarget, const CompilerOptions& options, Diagnostics& diagnostics);

// Whether `symbol` is reached through the GOT: preemptible symbols of
// position-independent code.
[[nodiscard]] bool reaches_through_got(const hir::Module& module, const CompilerOptions& options,
                                       const machine::SymbolOperand& symbol);

} // namespace cross::aarch64
