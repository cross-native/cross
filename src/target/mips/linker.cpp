// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/linker.hpp"

#include "linker/linker.hpp"
#include "target/mips/elf.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <string>

namespace cross::mips {
namespace {

using linker::InputFile;
using linker::InputSection;
using linker::OutputSection;
namespace elf = assembler::elf;

constexpr std::uint32_t arch_1 = 0x00000000;
constexpr std::uint32_t arch_2 = 0x10000000;
constexpr std::uint32_t arch_3 = 0x20000000;
constexpr std::uint32_t arch_4 = 0x30000000;
constexpr std::uint32_t arch_5 = 0x40000000;
constexpr std::uint32_t arch_32 = 0x50000000;
constexpr std::uint32_t arch_64 = 0x60000000;
constexpr std::uint32_t arch_32r2 = 0x70000000;
constexpr std::uint32_t arch_64r2 = 0x80000000;

struct ArchEdge {
    std::uint32_t child;
    std::uint32_t parent;
};

// ISA and machine extensions, each listed before what it extends (LLD's
// table, plus Allegrex).
constexpr ArchEdge arch_tree[] = {
    {arch_64r2 | 0x008e0000, arch_64r2}, {arch_64r2 | 0x008d0000, arch_64r2},
    {arch_64r2 | 0x008b0000, arch_64r2}, {arch_64r2 | 0x00a20000, arch_64r2},
    {arch_64 | 0x008a0000, arch_64},     {arch_64 | 0x008c0000, arch_64},
    {arch_64r2, arch_64},                {arch_64, arch_5},
    {arch_4 | 0x00980000, arch_4 | 0x00910000}, {arch_4 | 0x00910000, arch_4},
    {arch_4 | 0x00990000, arch_4},       {arch_5, arch_4},
    {arch_3 | 0x00880000, arch_3 | 0x00830000}, {arch_3 | 0x00870000, arch_3 | 0x00830000},
    {arch_3 | 0x00820000, arch_3},       {arch_3 | 0x00830000, arch_3},
    {arch_3 | 0x00850000, arch_3},       {arch_3 | 0x00920000, arch_3},
    {arch_3 | 0x00a00000, arch_3},       {arch_3 | 0x00a10000, arch_3},
    {arch_4, arch_3},                    {arch_32r2, arch_32},
    {arch_3, arch_2},                    {arch_32, arch_2},
    {arch_2 | 0x00840000, arch_2},       {arch_1 | 0x00810000, arch_1},
    {arch_2, arch_1},
};

// Whether code for `wanted` runs on `result`.
bool arch_matched(std::uint32_t wanted, std::uint32_t result) {
    if (wanted == result) return true;
    if (wanted == arch_32 && arch_matched(arch_64, result)) return true;
    if (wanted == arch_32r2 && arch_matched(arch_64r2, result)) return true;
    for (const auto& edge : arch_tree) {
        if (result == edge.child) {
            result = edge.parent;
            if (result == wanted) return true;
        }
    }
    return false;
}

std::string arch_name(std::uint32_t flags) {
    static constexpr std::array<const char*, 11> names{
        "mips1", "mips2", "mips3", "mips4", "mips5", "mips32", "mips64",
        "mips32r2", "mips64r2", "mips32r6", "mips64r6"};
    const auto arch = (flags & ef_mips_arch) >> 28U;
    std::string result = arch < names.size() ? names[arch] : "unknown ISA";
    if ((flags & ef_mips_mach) != 0) {
        char buffer[16];
        std::snprintf(buffer, sizeof buffer, "%#x", flags & ef_mips_mach);
        result += std::string(" (machine ") + buffer + ")";
    }
    return result;
}

std::string abi_name(std::uint32_t flags) {
    switch (flags & (ef_mips_abi | ef_mips_abi2)) {
    case 0: return "n64";
    case ef_mips_abi2: return "n32";
    case ef_mips_abi_o32: return "o32";
    case 0x2000: return "o64";
    case ef_mips_abi_eabi32: return "eabi32";
    case 0x4000: return "eabi64";
    default: return "unknown";
    }
}

// Val_GNU_MIPS_ABI_FP_* of .MIPS.abiflags.
enum : std::uint8_t { fp_any = 0, fp_double = 1, fp_xx = 5, fp_64 = 6, fp_64a = 7 };

std::string fp_name(std::uint8_t abi) {
    switch (abi) {
    case 0: return "any";
    case 1: return "-mdouble-float";
    case 2: return "-msingle-float";
    case 3: return "-msoft-float";
    case 4: return "-mgp32 -mfp64 (old)";
    case 5: return "-mfpxx";
    case 6: return "-mgp32 -mfp64";
    case 7: return "-mgp32 -mfp64 -mno-odd-spreg";
    default: return "unknown";
    }
}

// Positive when an object with floating-point ABI `a` may join objects with
// `b` and the result takes `a` (LLD's rule).
int compare_fp(std::uint8_t a, std::uint8_t b) {
    if (a == b) return 0;
    if (b == fp_any) return 1;
    if (b == fp_64a && a == fp_64) return 1;
    if (b != fp_xx) return -1;
    if (a == fp_double || a == fp_64 || a == fp_64a) return 1;
    return -1;
}

std::string relocation_name(std::uint32_t type) {
    switch (type) {
    case r_mips_none: return "R_MIPS_NONE";
    case r_mips_16: return "R_MIPS_16";
    case r_mips_32: return "R_MIPS_32";
    case 3: return "R_MIPS_REL32";
    case r_mips_26: return "R_MIPS_26";
    case r_mips_hi16: return "R_MIPS_HI16";
    case r_mips_lo16: return "R_MIPS_LO16";
    case r_mips_gprel16: return "R_MIPS_GPREL16";
    case r_mips_literal: return "R_MIPS_LITERAL";
    case 9: return "R_MIPS_GOT16";
    case r_mips_pc16: return "R_MIPS_PC16";
    case 11: return "R_MIPS_CALL16";
    case r_mips_gprel32: return "R_MIPS_GPREL32";
    case r_mips_64: return "R_MIPS_64";
    case r_mips_sub: return "R_MIPS_SUB";
    case r_mips_higher: return "R_MIPS_HIGHER";
    case r_mips_highest: return "R_MIPS_HIGHEST";
    case r_mips_pc32: return "R_MIPS_PC32";
    default: return "relocation type " + std::to_string(type);
    }
}

std::string hex(std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%#llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::int64_t sign_extend(std::uint64_t value, unsigned bits) {
    const auto shift = 64U - bits;
    return static_cast<std::int64_t>(value << shift) >> shift;
}

unsigned field_size(std::uint32_t type) {
    switch (type) {
    case r_mips_16: return 2;
    case r_mips_64: return 8;
    case r_mips_32: case r_mips_26: case r_mips_hi16: case r_mips_lo16: case r_mips_gprel16:
    case r_mips_literal: case r_mips_pc16: case r_mips_gprel32: case r_mips_higher:
    case r_mips_highest: case r_mips_pc32:
        return 4;
    default:
        return 0;
    }
}

class MipsLinker final : public linker::Target {
public:
    explicit MipsLinker(const assembler::ObjectInfo& output) : output_(output) {}

    bool add_object(const InputFile& file, Diagnostics& diagnostics) override {
        if (file.raw) return true;
        const auto flags = file.flags;
        if (!first_) {
            first_ = file.name();
            flags_ = flags & ~(ef_mips_pic | ef_mips_cpic | ef_mips_arch | ef_mips_mach);
            pic_ = flags & (ef_mips_pic | ef_mips_cpic);
            arch_ = flags & (ef_mips_arch | ef_mips_mach);
            return true;
        }
        bool ok = true;
        const auto abi = flags & (ef_mips_abi | ef_mips_abi2);
        const auto merged_abi = flags_ & (ef_mips_abi | ef_mips_abi2);
        if (abi != merged_abi) {
            diagnostics.command_error(file.name() + ": ABI '" + abi_name(abi) +
                                      "' is incompatible with ABI '" + abi_name(merged_abi) +
                                      "' of " + *first_);
            ok = false;
        }
        if ((flags & ef_mips_nan2008) != (flags_ & ef_mips_nan2008)) {
            diagnostics.command_error(file.name() + ": -mnan=" +
                                      ((flags & ef_mips_nan2008) ? "2008" : "legacy") +
                                      " is incompatible with " + *first_);
            ok = false;
        }
        if ((flags & ef_mips_fp64) != (flags_ & ef_mips_fp64)) {
            diagnostics.command_error(file.name() + ": -mfp" +
                                      ((flags & ef_mips_fp64) ? "64" : "32") +
                                      " is incompatible with " + *first_);
            ok = false;
        }
        const auto pic = flags & (ef_mips_pic | ef_mips_cpic);
        if ((pic != 0) != (pic_ != 0)) {
            diagnostics.warning({}, file.name() + ": linking " + (pic ? "abicalls" : "non-abicalls") +
                                        " code with " + (pic ? "non-abicalls" : "abicalls") +
                                        " code of " + *first_);
        }
        pic_ &= pic;
        const auto arch = flags & (ef_mips_arch | ef_mips_mach);
        if (!arch_matched(arch, arch_)) {
            if (!arch_matched(arch_, arch)) {
                diagnostics.command_error(file.name() + ": ISA " + arch_name(arch) +
                                          " is incompatible with " + arch_name(arch_) + " of " +
                                          *first_);
                ok = false;
            } else {
                arch_ = arch;
            }
        }
        flags_ |= flags & (ef_mips_noreorder | ef_mips_32bitmode | ef_mips_ase);
        return ok;
    }

    [[nodiscard]] std::uint32_t output_flags() const override {
        if (!first_) return output_.elf64 ? 0 : ef_mips_abi_o32;
        std::uint32_t pic = pic_;
        if (pic & ef_mips_pic) pic |= ef_mips_cpic;
        return flags_ | pic | arch_;
    }

    [[nodiscard]] std::uint64_t page_size() const override {
        return output_.elf64 ? 0x10000 : 0x1000;
    }

    [[nodiscard]] std::string_view common_section(std::uint32_t shndx) const override {
        if (shndx == shn_mips_scommon) return ".scommon";
        if (shndx == shn_mips_acommon) return "COMMON";
        return {};
    }

    [[nodiscard]] bool undefined_index(std::uint32_t shndx) const override {
        return shndx == shn_mips_sundefined;
    }

    void prune(InputSection& section,
               const std::function<bool(std::uint32_t)>& foreign) const override {
        // A 32-byte procedure descriptor whose first word refers to a
        // function the file does not contribute goes, as in GNU ld.
        constexpr std::uint64_t record = 32;
        if (section.name != ".pdr" || section.size % record != 0) return;
        std::vector<std::uint8_t> data;
        std::vector<assembler::Relocation> relocations;
        std::uint64_t removed = 0;
        for (std::uint64_t at = 0; at < section.size; at += record) {
            const auto first = std::find_if(section.relocations.begin(), section.relocations.end(),
                                            [&](const auto& entry) { return entry.offset == at; });
            if (first != section.relocations.end() && foreign(first->symbol.value)) {
                removed += record;
                continue;
            }
            data.insert(data.end(), section.data.begin() + static_cast<std::ptrdiff_t>(at),
                        section.data.begin() + static_cast<std::ptrdiff_t>(at + record));
            for (auto entry : section.relocations) {
                if (entry.offset < at || entry.offset >= at + record) continue;
                entry.offset -= removed;
                relocations.push_back(entry);
            }
        }
        if (removed == 0) return;
        section.data = std::move(data);
        section.size = section.data.size();
        section.relocations = std::move(relocations);
    }

    void merge_sections(std::span<InputSection* const> sections, Diagnostics& diagnostics) override {
        std::array<std::uint32_t, 5> masks{};
        std::array<std::uint8_t, 24> abiflags{};
        bool have_abiflags = false;
        for (auto* section : sections) {
            const auto& data = section->data;
            const auto fail = [&](std::string_view message) {
                diagnostics.command_error(section->file->name() + ": " + std::string(message) +
                                          " in " + section->name);
            };
            if (section->type == sht_mips_reginfo) {
                if (data.size() != 24) {
                    fail("invalid register information");
                    continue;
                }
                for (unsigned index = 0; index < 5; ++index) {
                    masks[index] |= static_cast<std::uint32_t>(read(data, 4 * index, 4));
                }
                section->file->gp0 = sign_extend(read(data, 20, 4), 32);
                if (!reginfo_) {
                    reginfo_ = section;
                } else {
                    section->discarded = true;
                }
            } else if (section->type == sht_mips_options) {
                for (std::size_t at = 0; at + 8 <= data.size();) {
                    const auto size = data[at + 1];
                    if (size < 8 || at + size > data.size()) {
                        fail("invalid option descriptor");
                        break;
                    }
                    if (data[at] == odk_reginfo) {
                        // Elf64_RegInfo has a pad word after the GPR mask.
                        const std::size_t cpr = output_.elf64 ? 16 : 12;
                        masks[0] |= static_cast<std::uint32_t>(read(data, at + 8, 4));
                        for (unsigned index = 0; index < 4 && at + cpr + 4 * index + 4 <= at + size; ++index) {
                            masks[1 + index] |= static_cast<std::uint32_t>(read(data, at + cpr + 4 * index, 4));
                        }
                        if (output_.elf64 && size >= 40) {
                            section->file->gp0 = static_cast<std::int64_t>(read(data, at + 32, 8));
                        } else if (!output_.elf64 && size >= 32) {
                            section->file->gp0 = sign_extend(read(data, at + 28, 4), 32);
                        }
                    }
                    at += size;
                }
                if (!options_) {
                    options_ = section;
                } else {
                    section->discarded = true;
                }
            } else if (section->type == sht_mips_abiflags) {
                if (data.size() < 24 || read(data, 0, 2) != 0) {
                    fail("invalid or unknown ABI flags");
                    continue;
                }
                if (!have_abiflags) {
                    std::copy(data.begin(), data.begin() + 24, abiflags.begin());
                    have_abiflags = true;
                    abiflags_ = section;
                    abiflags_file_ = section->file->name();
                    continue;
                }
                section->discarded = true;
                for (const std::size_t at : {2U, 3U, 4U, 5U, 6U}) {
                    abiflags[at] = std::max(abiflags[at], data[at]);
                }
                std::array<std::uint8_t, 24> merged = abiflags;
                if (read(data, 8, 4) > read(merged, 8, 4)) write_bytes(merged, 8, read(data, 8, 4), 4);
                for (const std::size_t at : {12U, 16U, 20U}) {
                    write_bytes(merged, at, read(merged, at, 4) | read(data, at, 4), 4);
                }
                // An incompatible floating-point ABI is a warning, as GNU ld
                // reports it; the earlier objects' ABI stays.
                const auto old_fp = abiflags[7];
                const auto new_fp = data[7];
                if (compare_fp(new_fp, old_fp) >= 0) {
                    merged[7] = new_fp;
                } else if (compare_fp(old_fp, new_fp) < 0) {
                    diagnostics.warning({}, section->file->name() + ": floating-point ABI '" +
                                                fp_name(new_fp) + "' is incompatible with '" +
                                                fp_name(old_fp) + "' of " + abiflags_file_);
                }
                abiflags = merged;
            }
        }
        if (reginfo_) {
            auto& data = reginfo_->data;
            data.assign(24, 0);
            for (unsigned index = 0; index < 5; ++index) write_bytes(data, 4 * index, masks[index], 4);
        }
        if (options_) {
            auto& data = options_->data;
            const std::size_t size = output_.elf64 ? 40 : 32;
            data.assign(size, 0);
            data[0] = odk_reginfo;
            data[1] = static_cast<std::uint8_t>(size);
            const std::size_t cpr = output_.elf64 ? 16 : 12;
            write_bytes(data, 8, masks[0], 4);
            for (unsigned index = 0; index < 4; ++index) write_bytes(data, cpr + 4 * index, masks[1 + index], 4);
            options_->size = size;
        }
        if (abiflags_) {
            abiflags_->data.assign(abiflags.begin(), abiflags.end());
            abiflags_->size = 24;
        }
    }

    void finish_sections(std::optional<std::uint64_t> gp) override {
        const auto value = gp.value_or(0);
        if (reginfo_) write_bytes(reginfo_->data, 20, value, 4);
        if (options_) {
            if (output_.elf64) {
                write_bytes(options_->data, 32, value, 8);
            } else {
                write_bytes(options_->data, 28, value, 4);
            }
        }
    }

    [[nodiscard]] std::vector<std::pair<std::uint32_t, OutputSection*>> extra_segments(
        std::span<OutputSection* const> sections) const override {
        // GNU ld's order: ABI flags, then register information.
        std::vector<std::pair<std::uint32_t, OutputSection*>> result;
        for (const auto type : {sht_mips_abiflags, sht_mips_reginfo}) {
            for (auto* section : sections) {
                if (section->type == type) {
                    result.emplace_back(type == sht_mips_abiflags ? pt_mips_abiflags : pt_mips_reginfo,
                                        section);
                    break;
                }
            }
        }
        return result;
    }

    void relocate(const linker::RelocationContext& context,
                  std::span<std::uint8_t> bytes) const override;

    [[nodiscard]] std::string default_script() const override {
        // A subset of GNU ld's script for the emulation: ABI sections first,
        // data on the next page at the same page offset, and _gp 32 KiB into
        // the small data.
        return std::string("SECTIONS\n{\n  . = ") + (output_.elf64 ? "0x120000000" : "0x400000") +
               ";\n"
               "  .MIPS.abiflags : { *(.MIPS.abiflags) }\n"
               "  .reginfo : { *(.reginfo) }\n"
               "  .MIPS.options : { *(.MIPS.options) }\n"
               "  .text : { _ftext = .; *(.text .stub .text.*) }\n"
               "  PROVIDE (__etext = .);\n"
               "  PROVIDE (_etext = .);\n"
               "  PROVIDE (etext = .);\n"
               "  .rodata : { *(.rodata .rodata.*) }\n"
               "  .eh_frame : { KEEP (*(.eh_frame)) }\n"
               "  . = ALIGN(CONSTANT(MAXPAGESIZE)) + (. & (CONSTANT(MAXPAGESIZE) - 1));\n"
               "  .data : { _fdata = .; *(.data .data.*) }\n"
               "  HIDDEN (_gp = ALIGN(16) + 0x7ff0);\n"
               "  .sdata : { *(.sdata .sdata.*) }\n"
               "  .lit8 : { *(.lit8) }\n"
               "  .lit4 : { *(.lit4) }\n"
               "  _edata = .;\n"
               "  PROVIDE (edata = .);\n"
               "  __bss_start = .;\n"
               "  _fbss = .;\n"
               "  .sbss : { *(.sbss .sbss.*) *(.scommon) }\n"
               "  .bss : { *(.bss .bss.*) *(COMMON) }\n"
               "  _end = .;\n"
               "  PROVIDE (end = .);\n"
               "}\n";
    }

    [[nodiscard]] bool local_label(std::string_view name) const override {
        // ELF32 MIPS assemblers start local labels with '$'.
        return !output_.elf64 && name.starts_with('$');
    }

    [[nodiscard]] std::vector<std::int64_t> addends(const InputSection& section) const override {
        std::vector<std::int64_t> result;
        result.reserve(section.relocations.size());
        for (std::size_t index = 0; index < section.relocations.size(); ++index) {
            const auto& relocation = section.relocations[index];
            if (section.rela) {
                result.push_back(relocation.addend);
                continue;
            }
            const auto symbol = relocation.symbol.value;
            const bool section_symbol = symbol < section.file->symbols.size() &&
                                        section.file->symbols[symbol].type == elf::stt_section;
            result.push_back(rel_addend(section, index, section_symbol).value);
        }
        return result;
    }

    bool store_addend(std::uint32_t type, std::int64_t addend,
                      std::span<std::uint8_t> field) const override {
        const auto value = static_cast<std::uint64_t>(addend);
        const auto replace = [&](std::uint32_t bits, std::uint32_t mask) {
            write_bytes(field, 0, insert_field(static_cast<std::uint32_t>(read(field, 0, 4)), bits, mask), 4);
        };
        switch (type) {
        case r_mips_16:
            write_bytes(field, 0, value, 2);
            return addend >= -32768 && addend <= 32767;
        case r_mips_32:
        case r_mips_gprel32:
        case r_mips_pc32:
            write_bytes(field, 0, value, 4);
            return true;
        case r_mips_64:
            write_bytes(field, 0, value, 8);
            return true;
        case r_mips_26:
            replace(jump26(value), jump26_mask);
            return (value & 3U) == 0 && addend >= 0 && addend < (std::int64_t{1} << 28);
        case r_mips_hi16: replace(high16(value), 0xffffU); return true;
        case r_mips_lo16: replace(low16(value), 0xffffU); return true;
        case r_mips_gprel16:
        case r_mips_literal:
            replace(low16(value), 0xffffU);
            return addend >= -32768 && addend <= 32767;
        case r_mips_pc16:
            replace(static_cast<std::uint32_t>(value >> 2U) & 0xffffU, 0xffffU);
            return (value & 3U) == 0 && addend >= -131072 && addend <= 131071;
        default:
            return false;
        }
    }

private:
    struct InPlace {
        std::int64_t value{};
        bool paired{true};
    };

    // The in-place addend of a REL relocation: a R_MIPS_HI16 takes the low
    // half from the next R_MIPS_LO16 of its symbol; the addend of a
    // R_MIPS_26 is unsigned against a section symbol (as in GNU ld).
    [[nodiscard]] InPlace rel_addend(const InputSection& section, std::size_t index,
                                     bool section_symbol) const {
        const auto& relocations = section.relocations;
        const auto& relocation = relocations[index];
        const auto type = relocation.type & 0xffU;
        const auto size = field_size(type);
        const auto& data = section.data;
        if (size == 0 || relocation.offset + size > data.size()) return {};
        const auto field = read(data, relocation.offset, size);
        switch (type) {
        case r_mips_16: return {sign_extend(field, 16)};
        case r_mips_64: return {static_cast<std::int64_t>(field)};
        case r_mips_26: {
            const auto target = (field & jump26_mask) << 2U;
            return {section_symbol ? static_cast<std::int64_t>(target) : sign_extend(target, 28)};
        }
        case r_mips_hi16: {
            InPlace result{static_cast<std::int64_t>((field & 0xffffU) << 16U), false};
            for (std::size_t next = index + 1; next < relocations.size(); ++next) {
                const auto& low = relocations[next];
                if ((low.type & 0xffU) != r_mips_lo16 || low.symbol != relocation.symbol) continue;
                if (low.offset + 4 <= data.size()) {
                    result.value += sign_extend(read(data, low.offset, 4) & 0xffffU, 16);
                }
                result.paired = true;
                break;
            }
            return result;
        }
        case r_mips_lo16:
        case r_mips_gprel16:
        case r_mips_literal:
            return {sign_extend(field & 0xffffU, 16)};
        case r_mips_pc16:
            return {sign_extend((field & 0xffffU) << 2U, 18)};
        default:
            return {sign_extend(field, 32)};
        }
    }

    template <typename Bytes>
    [[nodiscard]] std::uint64_t read(const Bytes& data, std::size_t offset, unsigned size) const {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < size; ++index) {
            const unsigned shift = output_.big_endian ? (size - 1 - index) * 8 : index * 8;
            value |= std::uint64_t{data[offset + index]} << shift;
        }
        return value;
    }

    template <typename Bytes>
    void write_bytes(Bytes& data, std::size_t offset, std::uint64_t value, unsigned size) const {
        for (unsigned index = 0; index < size; ++index) {
            const unsigned shift = output_.big_endian ? (size - 1 - index) * 8 : index * 8;
            data[offset + index] = static_cast<std::uint8_t>(value >> shift);
        }
    }

    assembler::ObjectInfo output_;
    std::optional<std::string> first_;
    std::uint32_t flags_{};
    std::uint32_t pic_{};
    std::uint32_t arch_{};
    InputSection* reginfo_{};
    InputSection* options_{};
    InputSection* abiflags_{};
    std::string abiflags_file_;
};

void MipsLinker::relocate(const linker::RelocationContext& context,
                          std::span<std::uint8_t> bytes) const {
    const auto& section = context.section();
    const auto& relocations = section.relocations;
    // In-place addends come from the input contents (section.data), not
    // from fields that earlier relocations of the section already filled.
    const bool rel = !section.rela;
    // Debugging and unwind tables may refer to removed code.
    const bool tombstone = !section.alloc() || section.name == ".eh_frame";
    const bool wide = output_.elf64;
    for (std::size_t index = 0; index < relocations.size(); ++index) {
        const auto& relocation = relocations[index];
        const auto type = relocation.type & 0xffU;
        const auto second = (relocation.type >> 8U) & 0xffU;
        const auto third = (relocation.type >> 16U) & 0xffU;
        if ((type == r_mips_none && second == r_mips_none && third == r_mips_none) || type == r_mips_jalr) {
            continue;
        }
        const auto symbol = context.symbol(relocation.symbol.value);
        std::string name = symbol.name.empty() ? std::string("<absolute>") : std::string(symbol.name);
        const auto offset = relocation.offset;
        // The field the last relocation of the record fills.
        std::uint32_t store_type = type;
        if (second == r_mips_64 && third == r_mips_none) {
            store_type = r_mips_64;
        } else if (second == r_mips_sub && (third == r_mips_hi16 || third == r_mips_lo16)) {
            store_type = third;
        } else if (second != r_mips_none || third != r_mips_none) {
            context.error(offset, "unsupported relocation combination " + relocation_name(type) + "/" +
                                      relocation_name(second) + "/" + relocation_name(third) +
                                      " against '" + name + "'");
            continue;
        }
        const auto size = field_size(type);
        const auto store_size = field_size(store_type);
        if (size == 0 || store_size == 0 || type == r_mips_sub) {
            context.error(offset, "unsupported " + relocation_name(type) + " against '" + name + "'" +
                                      (type == 9 || type == 11 ? " (PIC code is not supported)" : ""));
            continue;
        }
        if (offset > bytes.size() || store_size > bytes.size() - offset) {
            context.error(offset, relocation_name(type) + " lies outside its section");
            continue;
        }
        if (symbol.removed && !tombstone) {
            context.error(offset, relocation_name(type) + " refers to '" + name +
                                      "', which is in a discarded section");
            continue;
        }
        std::int64_t addend = relocation.addend;
        if (rel) {
            if (type == r_mips_higher || type == r_mips_highest) {
                context.error(offset, relocation_name(type) + " needs an explicit addend");
                continue;
            }
            const auto in_place = rel_addend(section, index, symbol.section);
            addend = in_place.value;
            if (!in_place.paired) {
                context.warning(offset, "can't find matching R_MIPS_LO16 relocation for "
                                        "R_MIPS_HI16 against '" + name + "'");
            }
        }
        // A section symbol names its section; the addend says where.
        if (symbol.section && addend != 0) name += (addend < 0 ? "-" : "+") + hex(static_cast<std::uint64_t>(addend < 0 ? -addend : addend));
        const std::uint64_t a = static_cast<std::uint64_t>(addend);
        const std::uint64_t p = context.place(offset);
        // S + A; in a merged section, the addend of a section symbol names
        // the input offset to map.
        std::uint64_t value = symbol.removed ? a
                              : symbol.merge ? context.merged(*symbol.merge, a)
                                             : symbol.value + a;
        switch (type) {
        case r_mips_gprel16:
        case r_mips_literal:
        case r_mips_gprel32: {
            const auto gp = context.gp();
            if (!gp) {
                context.error(offset, relocation_name(type) + " against '" + name +
                                          "' needs _gp, which nothing defines");
                continue;
            }
            const bool add_gp0 = type == r_mips_gprel32 || symbol.local;
            value = value - *gp + (add_gp0 ? static_cast<std::uint64_t>(section.file->gp0) : 0);
            break;
        }
        case r_mips_pc16:
        case r_mips_pc32:
            value -= p;
            break;
        default:
            break;
        }
        if (store_type != type && second == r_mips_sub) value = ~value + 1;
        if (!wide && store_type != r_mips_64) value &= 0xffffffffU;
        const auto signed_value = wide || store_type == r_mips_64 ? static_cast<std::int64_t>(value)
                                                                  : sign_extend(value, 32);
        const auto replace = [&](std::uint32_t bits, std::uint32_t mask) {
            const auto word = static_cast<std::uint32_t>(read(bytes, offset, 4));
            write_bytes(bytes, offset, insert_field(word, bits, mask), 4);
        };
        const auto out_of_range = [&](std::string_view what, std::int64_t low, std::int64_t high) {
            context.error(offset, relocation_name(store_type) + " against '" + name + "' out of range: " +
                                      std::to_string(signed_value) + " is not in [" +
                                      std::to_string(low) + ", " + std::to_string(high) + "]" +
                                      std::string(what));
        };
        switch (store_type) {
        case r_mips_16:
            if (!symbol.undefined && (signed_value < -32768 || signed_value > 32767)) {
                out_of_range("", -32768, 32767);
                continue;
            }
            write_bytes(bytes, offset, value, 2);
            break;
        case r_mips_32:
        case r_mips_gprel32:
        case r_mips_pc32:
            write_bytes(bytes, offset, value, 4);
            break;
        case r_mips_64:
            write_bytes(bytes, offset, wide ? value : static_cast<std::uint64_t>(sign_extend(value, 32)), 8);
            break;
        case r_mips_hi16: replace(high16(value), 0xffffU); break;
        case r_mips_lo16: replace(low16(value), 0xffffU); break;
        case r_mips_higher: replace(higher16(value), 0xffffU); break;
        case r_mips_highest: replace(highest16(value), 0xffffU); break;
        case r_mips_gprel16:
        case r_mips_literal:
            if (!symbol.undefined && (signed_value < -32768 || signed_value > 32767)) {
                out_of_range(" of _gp", -32768, 32767);
                continue;
            }
            replace(low16(value), 0xffffU);
            break;
        case r_mips_pc16:
            if (!symbol.undefined) {
                if ((value & 3U) != 0) {
                    context.error(offset, "branch to '" + name + "' is not word aligned");
                    continue;
                }
                if (signed_value < -131072 || signed_value > 131071) {
                    out_of_range(" bytes from the delay slot", -131072, 131071);
                    continue;
                }
            }
            replace(static_cast<std::uint32_t>(static_cast<std::uint64_t>(signed_value) >> 2U) & 0xffffU,
                    0xffffU);
            break;
        case r_mips_26: {
            // o32 symbol values and script addresses are both 32-bit
            // architectural addresses, so they compare without extension.
            const auto place = wide ? p + 4 : (p + 4) & 0xffffffffU;
            if (!symbol.undefined) {
                if ((value & 3U) != 0) {
                    context.error(offset, "jump target '" + name + "' (" + hex(value) +
                                              ") is not word aligned");
                    continue;
                }
                if ((value >> 28U) != (place >> 28U)) {
                    context.error(offset, "jump to '" + name + "' (" + hex(value) +
                                              ") leaves the 256 MiB region of its delay slot (" +
                                              hex(place) + ")");
                    continue;
                }
            }
            replace(jump26(value), jump26_mask);
            break;
        }
        default:
            break;
        }
    }
}

} // namespace

std::unique_ptr<linker::Target> make_linker_target(const assembler::ObjectInfo& output) {
    return std::make_unique<MipsLinker>(output);
}

} // namespace cross::mips
