// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/assembly_format.hpp"

#include <algorithm>

namespace cross {
namespace {

std::string quoted(std::string_view value) {
    return '"' + std::string(value) + '"';
}

std::string assembly_name(std::string_view name) {
    const bool simple = !name.empty() &&
        std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
                   ch == '$';
        });
    return simple ? std::string(name) : quoted(name);
}

std::string elf_flags(const AssemblySectionRequest& request) {
    std::string result;
    if (request.kind == AssemblySectionKind::Code) result = "ax";
    else if (request.kind == AssemblySectionKind::ReadOnlyData) result = "a";
    else result = "aw";
    if (request.kind == AssemblySectionKind::ThreadData ||
        request.kind == AssemblySectionKind::ThreadZeroFill) result += 'T';
    if (request.retain) result += 'R';
    return result;
}

std::string coff_flags(AssemblySectionKind kind) {
    switch (kind) {
    case AssemblySectionKind::Code: return "xr";
    case AssemblySectionKind::ReadOnlyData: return "dr";
    case AssemblySectionKind::WritableData: return "dw";
    case AssemblySectionKind::ZeroFill:
    case AssemblySectionKind::NoInit: return "bw";
    case AssemblySectionKind::ThreadData:
    case AssemblySectionKind::ThreadZeroFill: return "dw";
    }
    return {};
}

bool zero_fill(AssemblySectionKind kind) {
    return kind == AssemblySectionKind::ZeroFill ||
           kind == AssemblySectionKind::NoInit ||
           kind == AssemblySectionKind::ThreadZeroFill;
}

std::optional<std::string> macho_directive(
    const AssemblySectionRequest& request, std::string& error) {
    if (request.kind == AssemblySectionKind::ThreadData ||
        request.kind == AssemblySectionKind::ThreadZeroFill) {
        error = "runtime-free TLS sections are not implemented for Mach-O";
        return std::nullopt;
    }
    std::string_view segment;
    std::string_view section;
    std::string_view attributes;

    if (!request.custom) {
        switch (request.kind) {
        case AssemblySectionKind::Code:
            return ".section __TEXT,__text,regular,pure_instructions";
        case AssemblySectionKind::ReadOnlyData:
            return ".section __TEXT,__const";
        case AssemblySectionKind::WritableData:
            return ".section __DATA,__data";
        case AssemblySectionKind::ZeroFill:
            return ".section __DATA,__bss,zerofill";
        case AssemblySectionKind::NoInit:
            return ".section __DATA,__noinit,zerofill";
        case AssemblySectionKind::ThreadData:
        case AssemblySectionKind::ThreadZeroFill: break;
        }
    }

    const auto first_comma = request.name.find(',');
    if (first_comma == std::string_view::npos) {
        segment = request.kind == AssemblySectionKind::Code ||
                          request.kind == AssemblySectionKind::ReadOnlyData
                      ? "__TEXT"
                      : "__DATA";
        section = request.name;
    } else {
        segment = request.name.substr(0, first_comma);
        const auto remainder = request.name.substr(first_comma + 1);
        const auto second_comma = remainder.find(',');
        section = remainder.substr(0, second_comma);
        if (second_comma != std::string_view::npos) {
            attributes = remainder.substr(second_comma + 1);
        }
    }
    if (segment.empty() || section.empty()) {
        error = "Mach-O section must have nonempty segment and section names";
        return std::nullopt;
    }
    if (segment.size() > 16 || section.size() > 16) {
        error = "Mach-O segment and section names may contain at most 16 bytes";
        return std::nullopt;
    }

    std::string result = ".section " + assembly_name(segment) + ',' +
                         assembly_name(section);
    if (!attributes.empty()) {
        result += ',' + std::string(attributes);
    } else if (request.kind == AssemblySectionKind::Code) {
        result += ",regular,pure_instructions";
    } else if (zero_fill(request.kind)) {
        result += ",zerofill";
    }
    return result;
}

} // namespace

std::optional<std::string> assembly_section_directive(
    ObjectFormat format, const AssemblySectionRequest& request,
    std::string& error) {
    error.clear();
    if (request.name.empty()) {
        error = "section name cannot be empty";
        return std::nullopt;
    }
    switch (format) {
    case ObjectFormat::Elf:
        if (!request.retain && request.name == ".text") return ".text";
        if (!request.retain && request.name == ".data") return ".data";
        if (!request.retain && request.name == ".bss") return ".bss";
        return ".section " + quoted(request.name) + ",\"" +
               elf_flags(request) + "\"," +
               (zero_fill(request.kind) ? "@nobits" : "@progbits");
    case ObjectFormat::Coff:
        if (request.kind == AssemblySectionKind::ThreadData ||
            request.kind == AssemblySectionKind::ThreadZeroFill) {
            if (!request.name.starts_with(".tls$")) {
                error = "COFF TLS sections must use a .tls$ name so the PE linker includes them in the static TLS image";
                return std::nullopt;
            }
        }
        if (request.name == ".text") return ".text";
        if (request.name == ".data") return ".data";
        if (request.name == ".bss") return ".bss";
        return ".section " + quoted(request.name) + ",\"" +
               coff_flags(request.kind) + '"';
    case ObjectFormat::MachO:
        return macho_directive(request, error);
    case ObjectFormat::Unsupported:
        error = "selected object format has no assembly section syntax";
        return std::nullopt;
    }
    error = "selected object format has no assembly section syntax";
    return std::nullopt;
}

bool assembly_uses_dwarf_cfi(ObjectFormat format) {
    return format == ObjectFormat::Elf || format == ObjectFormat::MachO;
}

} // namespace cross
