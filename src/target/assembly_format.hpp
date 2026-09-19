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
};

enum class AssemblySymbolVisibility { Default, Hidden, Protected, Internal };

struct AssemblySymbolRequest {
    std::string_view name;
    bool external{};
    bool definition{true};
    bool weak{};
    AssemblySymbolVisibility visibility{AssemblySymbolVisibility::Default};
};

// Returns one complete assembler directive without a trailing newline.  The
// logical name uses Cross/ELF-style defaults (for example `.text` or
// `.rodata.foo`); Mach-O translation is performed here.  A custom Mach-O name
// may be either a section name or a native `segment,section[,attributes]`
// spelling.
[[nodiscard]] std::optional<std::string> assembly_section_directive(
    ObjectFormat format, const AssemblySectionRequest& request,
    std::string& error);

// Returns complete binding/visibility directives without a trailing newline.
// An empty result is valid for a local symbol on formats without a local
// directive. Type metadata remains the responsibility of the target emitter.
[[nodiscard]] std::optional<std::string> assembly_symbol_directives(
    ObjectFormat format, const AssemblySymbolRequest& request,
    std::string& error);

[[nodiscard]] bool assembly_uses_dwarf_cfi(ObjectFormat format);

} // namespace cross
