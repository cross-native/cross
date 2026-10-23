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

namespace aarch64 {

// Assembles GNU-syntax A64 assembly into a little-endian ELF64 relocatable
// object, with the instructions of the loaded instruction table that the
// subtarget's features allow.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, const Subtarget& subtarget,
    const assembler::Options& options, SourceManager& manager,
    Diagnostics& diagnostics);

} // namespace aarch64
} // namespace cross
