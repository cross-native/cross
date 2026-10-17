// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "assembler/assembler.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace cross::assembler {

// Writes a COFF relocatable object: sections, their relocations (one list
// per section, in-place addends already applied), and the symbol table with
// section definitions, COMDAT and weak-external records, and `.file` names.
[[nodiscard]] std::vector<std::uint8_t> write_coff(
    Assembler& assembler, const std::vector<std::vector<Relocation>>& relocations,
    const std::vector<std::string>& file_names);

} // namespace cross::assembler
