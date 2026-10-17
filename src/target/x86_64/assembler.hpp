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

namespace x86_64 {

// Assembles AT&T-syntax x86-64 assembly into an ELF, COFF, or Mach-O
// relocatable object, as the subtarget's object format selects.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, const Subtarget& subtarget,
    const assembler::Options& options, SourceManager& manager,
    Diagnostics& diagnostics);

} // namespace x86_64
} // namespace cross
