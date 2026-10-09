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
#include <string>

namespace cross::mips {

// The ELF ABI tag an ABI model requests through `elf_abi_tag`, which model
// loading checks against the target's tags. Default keeps the tag the object
// writer derives from the triple.
enum class ElfAbiTag : std::uint8_t { Default, Eabi32 };
[[nodiscard]] ElfAbiTag elf_abi_tag(const AbiEntry& abi);

[[nodiscard]] machine::Module lower_managed_machine(
    const mir::ManagedModule& managed, const hir::Module& hir_module,
    const Subtarget& subtarget, const CompilerOptions& options,
    Diagnostics& diagnostics);

[[nodiscard]] std::string emit_managed_machine_assembly(
    machine::Module& module, const mir::ManagedModule& managed,
    const hir::Module& hir_module, const Subtarget& subtarget,
    const CompilerOptions& options, Diagnostics& diagnostics);

} // namespace cross::mips
