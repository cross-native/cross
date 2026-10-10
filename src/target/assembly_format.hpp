// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/subtarget.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace cross {

// Logical section properties shared by target assembly emitters.  Object-file
// spelling belongs here rather than in the driver or individual MIR printers.
enum class AssemblySectionKind {
    Code,
    ReadOnlyData,
    WritableData,
    ZeroFill,
    NoInit,
    ThreadData,
    ThreadZeroFill,
};

struct AssemblySectionRequest {
    std::string_view name;
    AssemblySectionKind kind{AssemblySectionKind::WritableData};
    bool custom{};
    bool retain{};
    // Assembler spelling of the symbol whose mergeable definition keys this
    // section's COMDAT group, or empty. An associated section, such as a
    // jump table, belongs to the group without defining that symbol. Mach-O
    // has no groups and merges weak definitions instead.
    std::string_view group{};
    bool associated{};
};

enum class AssemblySymbolVisibility { Default, Hidden, Protected, Internal };

struct AssemblySymbolRequest {
    std::string_view name;
    bool external{};
    bool definition{true};
    bool weak{};
    AssemblySymbolVisibility visibility{AssemblySymbolVisibility::Default};
    // A definition that other objects may duplicate. ELF and COFF place it
    // in a COMDAT group section keyed by this symbol; Mach-O marks it weak.
    bool mergeable{};
};

// Returns one complete assembler directive without a trailing newline.  The
// logical name uses Cross/ELF-style defaults (for example `.text` or
// `.rodata.foo`); Mach-O translation is performed here.  A custom Mach-O name
// may be either a section name or a native `segment,section[,attributes]`
// spelling.
[[nodiscard]] std::optional<std::string> assembly_section_directive(
    ObjectFormat format, const AssemblySectionRequest& request,
    std::string& error);

// The read-only section of a function's jump tables and literals: for a
// function in a section of its own, `.rodata.LINK` (COFF `.rdata$LINK`) in
// the COMDAT group `group`, if any; otherwise `.rodata` (`.rdata`).
[[nodiscard]] std::optional<std::string> function_data_section_directive(
    ObjectFormat format, std::string_view link_name, bool own_section,
    std::string_view group, std::string& error);

// Returns complete binding/visibility directives without a trailing newline.
// An empty result is valid for a local symbol on formats without a local
// directive. Type metadata remains the responsibility of the target emitter.
[[nodiscard]] std::optional<std::string> assembly_symbol_directives(
    ObjectFormat format, const AssemblySymbolRequest& request,
    std::string& error);

[[nodiscard]] bool assembly_uses_dwarf_cfi(ObjectFormat format);

// Returns the assembler spelling of the object-file symbol for a link name:
// Mach-O prefixes C-level names with `_`, and a name outside the assembler's
// identifier characters is quoted.
[[nodiscard]] std::string assembly_symbol(ObjectFormat format,
                                          std::string_view link_name);

} // namespace cross
