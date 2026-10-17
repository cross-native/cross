// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/mips/linker.hpp"

#include "linker/linker.hpp"
#include "target/mips/elf.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <map>
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

// GNU object attributes of the "gnu" vendor for the whole file: each tag's
// integer and string (Tag_compatibility has both, other odd tags a string,
// even tags an integer).
struct Attribute {
    std::uint64_t number{};
    std::string text;
};
using Attributes = std::map<std::uint64_t, Attribute>;
constexpr std::uint64_t tag_file = 1;
constexpr std::uint64_t tag_mips_abi_fp = 4;
constexpr std::uint64_t tag_mips_abi_msa = 8;
constexpr std::uint64_t tag_compatibility = 32;

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
        bool ok = merge_attributes(file, diagnostics);
        const auto flags = file.flags;
        if (!first_) {
            first_ = file.name();
            flags_ = flags & ~(ef_mips_pic | ef_mips_cpic | ef_mips_arch | ef_mips_mach);
            pic_ = flags & (ef_mips_pic | ef_mips_cpic);
            arch_ = flags & (ef_mips_arch | ef_mips_mach);
            return ok;
        }
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
                section->file->gp0 = sign_extend(read(data, 20, 4), 32);
                // GNU ld links only the first .reginfo (a link-once section),
                // so its masks are the output's.
                if (!reginfo_) {
                    reginfo_ = section;
                    for (unsigned index = 0; index < 5; ++index) {
                        reginfo_masks_[index] = static_cast<std::uint32_t>(read(data, 4 * index, 4));
                    }
                } else {
                    section->discarded = true;
                    section->combined = reginfo_;
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
                    section->combined = options_;
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
                    continue;
                }
                section->discarded = true;
                section->combined = abiflags_;
                for (const std::size_t at : {2U, 3U, 4U, 5U, 6U}) {
                    abiflags[at] = std::max(abiflags[at], data[at]);
                }
                if (read(data, 8, 4) > read(abiflags, 8, 4)) write_bytes(abiflags, 8, read(data, 8, 4), 4);
                for (const std::size_t at : {12U, 16U, 20U}) {
                    write_bytes(abiflags, at, read(abiflags, at, 4) | read(data, at, 4), 4);
                }
            } else if (section->type == elf::sht_gnu_attributes) {
                if (!gnu_attributes_) {
                    gnu_attributes_ = section;
                } else {
                    section->discarded = true;
                    section->combined = gnu_attributes_;
                }
            }
        }
        // The floating-point ABI is the objects' merged attribute (GNU ld).
        if (attributes_initialized_) {
            const auto fp = attributes_.find(tag_mips_abi_fp);
            abiflags[7] = static_cast<std::uint8_t>(fp == attributes_.end() ? 0 : fp->second.number);
        }
        if (gnu_attributes_) {
            gnu_attributes_->data = serialize_attributes();
            gnu_attributes_->size = gnu_attributes_->data.size();
        }
        if (reginfo_) {
            auto& data = reginfo_->data;
            data.assign(24, 0);
            for (unsigned index = 0; index < 5; ++index) write_bytes(data, 4 * index, reginfo_masks_[index], 4);
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

    [[nodiscard]] bool sections_end_locals(const linker::OutputFormat& format) const override {
        // IRIX 5 compatible ELF32 counts only section symbols as local.
        return !format.traditional && !output_.elf64;
    }

    void finish_output_section(std::string_view name, const linker::OutputFormat& format, std::uint32_t& type,
                               std::uint64_t& flags, std::uint64_t& entry_size) const override {
        // GNU ld's MIPS output section conventions by name; the IRIX-
        // compatible formats (all but the traditional ones) differ.
        const bool irix = !format.traditional;
        if (name == ".reginfo") {
            type = sht_mips_reginfo;
            entry_size = irix ? 1 : 24;
        } else if (name == ".mdebug") {
            type = sht_mips_debug;
            entry_size = 1;
        } else if (name == ".got" || name == ".srdata" || name == ".sdata" || name == ".sbss" ||
                   name == ".lit4" || name == ".lit8") {
            flags |= shf_mips_gprel;
        } else if (name == ".MIPS.options" || name == ".options") {
            type = sht_mips_options;
            entry_size = 1;
            flags |= shf_mips_nostrip;
        } else if (name.starts_with(".MIPS.abiflags")) {
            type = sht_mips_abiflags;
            entry_size = 24;
        } else if (name.starts_with(".debug_") || name.starts_with(".gnu.debuglto_.debug_") ||
                   name.starts_with(".zdebug_") || name.starts_with(".gnu.debuglto_.zdebug_")) {
            type = sht_mips_dwarf;
            if (irix && name.starts_with(".debug_frame")) flags |= shf_mips_nostrip;
        }
    }

    [[nodiscard]] std::string default_script(bool relocatable) const override {
        if (relocatable) {
            // GNU ld's relocatable layout for the MIPS emulations.
            return "SECTIONS\n{\n"
                   "  .MIPS.abiflags : { *(.MIPS.abiflags) }\n"
                   "  .reginfo : { *(.reginfo) }\n"
                   "  .init : { *(.init) }\n"
                   "  .text : { *(.text .stub) *(.gnu.warning) }\n"
                   "  .fini : { *(.fini) }\n"
                   "  .rodata : { *(.rodata) }\n"
                   "  .rodata1 : { *(.rodata1) }\n"
                   "  .sdata2 : { *(.sdata2) }\n"
                   "  .sbss2 : { *(.sbss2) }\n"
                   "  .eh_frame_hdr : { *(.eh_frame_hdr) }\n"
                   "  .eh_frame : { *(.eh_frame) }\n"
                   "  .gcc_except_table : { *(.gcc_except_table) }\n"
                   "  .tdata : { *(.tdata) }\n"
                   "  .tbss : { *(.tbss) }\n"
                   "  .jcr : { *(.jcr) }\n"
                   "  .data : { *(.data) }\n"
                   "  .data1 : { *(.data1) }\n"
                   "  .got.plt : { *(.got.plt) }\n"
                   "  .got : { *(.got) }\n"
                   "  .sdata : { *(.sdata) }\n"
                   "  .lit8 : { *(.lit8) }\n"
                   "  .lit4 : { *(.lit4) }\n"
                   "  .sbss : { *(.sbss) }\n"
                   "  .bss : { *(.bss) }\n"
                   "  .stab : { *(.stab) }\n"
                   "  .stabstr : { *(.stabstr) }\n"
                   "  .comment : { *(.comment) }\n"
                   "  .debug : { *(.debug) }\n"
                   "  .line : { *(.line) }\n"
                   "  .debug_srcinfo : { *(.debug_srcinfo) }\n"
                   "  .debug_sfnames : { *(.debug_sfnames) }\n"
                   "  .debug_aranges : { *(.debug_aranges) }\n"
                   "  .debug_pubnames : { *(.debug_pubnames) }\n"
                   "  .debug_info : { *(.debug_info) }\n"
                   "  .debug_abbrev : { *(.debug_abbrev) }\n"
                   "  .debug_line : { *(.debug_line) }\n"
                   "  .debug_frame : { *(.debug_frame) }\n"
                   "  .debug_str : { *(.debug_str) }\n"
                   "  .debug_loc : { *(.debug_loc) }\n"
                   "  .debug_macinfo : { *(.debug_macinfo) }\n"
                   "  .debug_pubtypes : { *(.debug_pubtypes) }\n"
                   "  .debug_ranges : { *(.debug_ranges) }\n"
                   "  .debug_addr : { *(.debug_addr) }\n"
                   "  .debug_line_str : { *(.debug_line_str) }\n"
                   "  .debug_loclists : { *(.debug_loclists) }\n"
                   "  .debug_macro : { *(.debug_macro) }\n"
                   "  .debug_names : { *(.debug_names) }\n"
                   "  .debug_rnglists : { *(.debug_rnglists) }\n"
                   "  .debug_str_offsets : { *(.debug_str_offsets) }\n"
                   "  .debug_sup : { *(.debug_sup) }\n"
                   "  .gnu.attributes : { *(.gnu.attributes) }\n"
                   "  .gptab.sdata : { *(.gptab.sdata) }\n"
                   "  .gptab.sbss : { *(.gptab.sbss) }\n"
                   "  .mdebug.abi32 : { *(.mdebug.abi32) }\n"
                   "  .mdebug.abiN32 : { *(.mdebug.abiN32) }\n"
                   "  .mdebug.abi64 : { *(.mdebug.abi64) }\n"
                   "  .mdebug.abiO64 : { *(.mdebug.abiO64) }\n"
                   "  .mdebug.eabi32 : { *(.mdebug.eabi32) }\n"
                   "  .mdebug.eabi64 : { *(.mdebug.eabi64) }\n"
                   "  .gcc_compiled_long32 : { *(.gcc_compiled_long32) }\n"
                   "  .gcc_compiled_long64 : { *(.gcc_compiled_long64) }\n"
                   "}\n";
        }
        // A subset of GNU ld's script for the emulation: ABI sections first,
        // data placed by DATA_SEGMENT_ALIGN, and _gp 32 KiB into the small
        // data.
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
               "  .eh_frame_hdr : { *(.eh_frame_hdr) }\n"
               "  .eh_frame : ONLY_IF_RO { KEEP (*(.eh_frame)) }\n"
               "  .gcc_except_table : ONLY_IF_RO { *(.gcc_except_table .gcc_except_table.*) }\n"
               "  . = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));\n"
               "  .eh_frame : ONLY_IF_RW { KEEP (*(.eh_frame)) }\n"
               "  .gcc_except_table : ONLY_IF_RW { *(.gcc_except_table .gcc_except_table.*) }\n"
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
               "  . = DATA_SEGMENT_END (.);\n"
               "  .stab 0 : { *(.stab) }\n"
               "  .stabstr 0 : { *(.stabstr) }\n"
               "  .comment 0 (INFO) : { *(.comment) }\n"
               "  .debug 0 : { *(.debug) }\n"
               "  .line 0 : { *(.line) }\n"
               "  .debug_srcinfo 0 : { *(.debug_srcinfo) }\n"
               "  .debug_sfnames 0 : { *(.debug_sfnames) }\n"
               "  .debug_aranges 0 : { *(.debug_aranges) }\n"
               "  .debug_pubnames 0 : { *(.debug_pubnames) }\n"
               "  .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }\n"
               "  .debug_abbrev 0 : { *(.debug_abbrev) }\n"
               "  .debug_line 0 : { *(.debug_line .debug_line.* .debug_line_end) }\n"
               "  .debug_frame 0 : { *(.debug_frame) }\n"
               "  .debug_str 0 : { *(.debug_str) }\n"
               "  .debug_loc 0 : { *(.debug_loc) }\n"
               "  .debug_macinfo 0 : { *(.debug_macinfo) }\n"
               "  .debug_pubtypes 0 : { *(.debug_pubtypes) }\n"
               "  .debug_ranges 0 : { *(.debug_ranges) }\n"
               "  .debug_addr 0 : { *(.debug_addr) }\n"
               "  .debug_line_str 0 : { *(.debug_line_str) }\n"
               "  .debug_loclists 0 : { *(.debug_loclists) }\n"
               "  .debug_macro 0 : { *(.debug_macro) }\n"
               "  .debug_names 0 : { *(.debug_names) }\n"
               "  .debug_rnglists 0 : { *(.debug_rnglists) }\n"
               "  .debug_str_offsets 0 : { *(.debug_str_offsets) }\n"
               "  .debug_sup 0 : { *(.debug_sup) }\n"
               "  .gnu.attributes 0 : { KEEP (*(.gnu.attributes)) }\n"
               "  .mdebug.abi32 0 : { KEEP (*(.mdebug.abi32)) }\n"
               "  .mdebug.abi64 0 : { KEEP (*(.mdebug.abi64)) }\n"
               "  .mdebug.eabi32 0 : { KEEP (*(.mdebug.eabi32)) }\n"
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
    // The "gnu" Tag_File attributes of an attributes section.
    [[nodiscard]] Attributes parse_attributes(const std::vector<std::uint8_t>& data) const {
        Attributes result;
        if (data.empty() || data[0] != 'A') return result;
        std::size_t at = 1;
        const auto uleb = [&](std::size_t& position, std::size_t end) {
            std::uint64_t value = 0;
            unsigned shift = 0;
            while (position < end) {
                const auto byte = data[position++];
                if (shift < 64) value |= std::uint64_t{byte & 0x7fU} << shift;
                shift += 7;
                if ((byte & 0x80) == 0) break;
            }
            return value;
        };
        while (at + 4 <= data.size()) {
            const auto length = read(data, at, 4);
            if (length < 4 || length > data.size() - at) break;
            const auto end = at + length;
            std::size_t position = at + 4;
            std::string vendor;
            while (position < end && data[position] != 0) vendor += static_cast<char>(data[position++]);
            ++position;
            while (vendor == "gnu" && position + 5 <= end) {
                const auto start = position;
                const auto tag = uleb(position, end);
                if (position + 4 > end) break;
                const auto size = read(data, position, 4);
                position += 4;
                const auto stop = start + size;
                if (size < 5 || stop > end) break;
                while (tag == tag_file && position < stop) {
                    const auto attribute = uleb(position, stop);
                    auto& value = result[attribute];
                    if (attribute == tag_compatibility || (attribute & 1) == 0) value.number = uleb(position, stop);
                    if (attribute == tag_compatibility || (attribute & 1) != 0) {
                        while (position < stop && data[position] != 0) value.text += static_cast<char>(data[position++]);
                        ++position;
                    }
                }
                position = stop;
            }
            at = end;
        }
        return result;
    }

    [[nodiscard]] std::vector<std::uint8_t> serialize_attributes() const {
        std::vector<std::uint8_t> body;
        const auto uleb = [&](std::uint64_t value) {
            do {
                std::uint8_t byte = value & 0x7fU;
                value >>= 7;
                if (value != 0) byte |= 0x80U;
                body.push_back(byte);
            } while (value != 0);
        };
        for (const auto& [tag, value] : attributes_) {
            if (value.number == 0 && value.text.empty()) continue;
            uleb(tag);
            if (tag == tag_compatibility || (tag & 1) == 0) uleb(value.number);
            if (tag == tag_compatibility || (tag & 1) != 0) {
                body.insert(body.end(), value.text.begin(), value.text.end());
                body.push_back(0);
            }
        }
        if (body.empty()) return {};
        std::vector<std::uint8_t> data{'A', 0, 0, 0, 0, 'g', 'n', 'u', 0, static_cast<std::uint8_t>(tag_file), 0, 0, 0, 0};
        write_bytes(data, 1, 13 + body.size(), 4);
        write_bytes(data, 10, 5 + body.size(), 4);
        data.insert(data.end(), body.begin(), body.end());
        return data;
    }

    // GNU ld's merge of object attributes: the first object's are copied,
    // then the floating-point and MSA ABIs merge and Tag_compatibility must
    // agree. An object without a floating-point attribute takes the one in
    // its .MIPS.abiflags; objects with nothing but empty .text, .data, and
    // .bss sections take no part.
    bool merge_attributes(const InputFile& file, Diagnostics& diagnostics) {
        const InputSection* attributes = nullptr;
        const InputSection* abiflags = nullptr;
        bool empty = true;
        for (const auto& section : file.sections) {
            if (!section) continue;
            if (section->type == elf::sht_gnu_attributes && !attributes) attributes = section.get();
            if (section->type == sht_mips_abiflags && !abiflags) abiflags = section.get();
            const auto& name = section->name;
            if (name != ".reginfo" && name != ".mdebug" &&
                (section->size != 0 || (name != ".text" && name != ".data" && name != ".bss"))) {
                empty = false;
            }
        }
        if (empty) return true;
        auto in = attributes ? parse_attributes(attributes->data) : Attributes{};
        if (abiflags && abiflags->data.size() >= 24 && in[tag_mips_abi_fp].number == fp_any) {
            in[tag_mips_abi_fp].number = abiflags->data[7];
        }
        const auto in_fp = static_cast<std::uint8_t>(in[tag_mips_abi_fp].number);
        if (!attributes_initialized_) {
            attributes_ = std::move(in);
            attributes_initialized_ = true;
            if (in_fp != fp_any) fp_file_ = file.name();
            return true;
        }
        if (fp_file_.empty() && in_fp != fp_any) fp_file_ = file.name();
        auto& out = attributes_[tag_mips_abi_fp].number;
        const auto out_fp = static_cast<std::uint8_t>(out);
        if (in_fp != out_fp) {
            const auto set = [&] {
                out = in_fp;
                fp_file_ = file.name();
            };
            const auto hard = [](std::uint8_t fp) { return fp == fp_double || fp == fp_64 || fp == fp_64a; };
            if (out_fp == fp_any) {
                out = in_fp;
            } else if (out_fp == fp_xx && hard(in_fp)) {
                set();
            } else if (in_fp == fp_xx && hard(out_fp)) {
            } else if (out_fp == fp_64a && in_fp == fp_64) {
                set();
            } else if (in_fp == fp_64a && out_fp == fp_64) {
            } else if (in_fp != fp_any) {
                diagnostics.warning({}, file.name() + ": floating-point ABI '" + fp_name(in_fp) +
                                            "' is incompatible with '" + fp_name(out_fp) + "' of " + fp_file_);
            }
        }
        auto& out_msa = attributes_[tag_mips_abi_msa].number;
        const auto in_msa = in[tag_mips_abi_msa].number;
        if (in_msa != out_msa) {
            if (out_msa == 0) {
                out_msa = in_msa;
            } else if (in_msa != 0) {
                diagnostics.warning({}, file.name() + ": MSA ABI " + std::to_string(in_msa) +
                                            " is incompatible with MSA ABI " + std::to_string(out_msa));
            }
        }
        const auto& in_compatibility = in[tag_compatibility];
        const auto& out_compatibility = attributes_[tag_compatibility];
        if (in_compatibility.number != 0 && in_compatibility.text != "gnu") {
            diagnostics.command_error(file.name() + ": object has vendor-specific contents that must be "
                                                    "processed by the '" + in_compatibility.text + "' toolchain");
            return false;
        }
        if (in_compatibility.number != out_compatibility.number ||
            (in_compatibility.number != 0 && in_compatibility.text != out_compatibility.text)) {
            diagnostics.command_error(file.name() + ": object tag '" + std::to_string(in_compatibility.number) +
                                      ", " + in_compatibility.text + "' is incompatible with tag '" +
                                      std::to_string(out_compatibility.number) + ", " + out_compatibility.text +
                                      "'");
            return false;
        }
        return true;
    }

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
    std::array<std::uint32_t, 5> reginfo_masks_{};
    InputSection* options_{};
    InputSection* abiflags_{};
    InputSection* gnu_attributes_{};
    Attributes attributes_;
    bool attributes_initialized_{};
    // The object that set the floating-point ABI.
    std::string fp_file_;
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
