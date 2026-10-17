// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "assembler/assembler.hpp"

#include <cstdint>
#include <vector>

namespace cross::assembler {

// The addresses Mach-O gives sections in an object: sections with file
// contents first, then zero-fill sections, each at its alignment.
[[nodiscard]] std::vector<std::uint64_t> macho_section_addresses(const Assembler& assembler);

// Writes a 64-bit Mach-O relocatable object (one segment, LC_SYMTAB and
// LC_DYSYMTAB). Relocations are per section in recording order; the writer
// emits them in reverse, as LLVM MC and Apple's assembler do.
[[nodiscard]] std::vector<std::uint8_t> write_macho(
    Assembler& assembler, const std::vector<std::vector<Relocation>>& relocations,
    bool subsections_via_symbols);

} // namespace cross::assembler
