// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/source.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace cross {

class Subtarget;

namespace assembler {
struct Options;
}

namespace mips {

// Assembles GNU-syntax MIPS assembly into an ELF relocatable object whose
// class, byte order, ABI tag, ISA, and floating-point ABI follow the
// resolved subtarget.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, const Subtarget& subtarget,
    const assembler::Options& options, SourceManager& manager,
    Diagnostics& diagnostics);

} // namespace mips
} // namespace cross
