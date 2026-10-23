// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/linker.hpp"

#include "linker/linker.hpp"
#include "target/aarch64/elf.hpp"

#include <algorithm>
#include <array>
#include <compare>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cross::aarch64 {
namespace {

using linker::InputFile;
using linker::InputSection;
using linker::OutputSection;
namespace elf = assembler::elf;

constexpr std::pair<std::uint32_t, const char*> relocation_names[] = {
    {0x000, "R_AARCH64_NONE"}, {0x101, "R_AARCH64_ABS64"}, {0x102, "R_AARCH64_ABS32"},
    {0x103, "R_AARCH64_ABS16"}, {0x104, "R_AARCH64_PREL64"}, {0x105, "R_AARCH64_PREL32"},
    {0x106, "R_AARCH64_PREL16"}, {0x107, "R_AARCH64_MOVW_UABS_G0"}, {0x108, "R_AARCH64_MOVW_UABS_G0_NC"},
    {0x109, "R_AARCH64_MOVW_UABS_G1"}, {0x10a, "R_AARCH64_MOVW_UABS_G1_NC"}, {0x10b, "R_AARCH64_MOVW_UABS_G2"},
    {0x10c, "R_AARCH64_MOVW_UABS_G2_NC"}, {0x10d, "R_AARCH64_MOVW_UABS_G3"}, {0x10e, "R_AARCH64_MOVW_SABS_G0"},
    {0x10f, "R_AARCH64_MOVW_SABS_G1"}, {0x110, "R_AARCH64_MOVW_SABS_G2"}, {0x111, "R_AARCH64_LD_PREL_LO19"},
    {0x112, "R_AARCH64_ADR_PREL_LO21"}, {0x113, "R_AARCH64_ADR_PREL_PG_HI21"},
    {0x114, "R_AARCH64_ADR_PREL_PG_HI21_NC"}, {0x115, "R_AARCH64_ADD_ABS_LO12_NC"},
    {0x116, "R_AARCH64_LDST8_ABS_LO12_NC"}, {0x117, "R_AARCH64_TSTBR14"}, {0x118, "R_AARCH64_CONDBR19"},
    {0x11a, "R_AARCH64_JUMP26"}, {0x11b, "R_AARCH64_CALL26"}, {0x11c, "R_AARCH64_LDST16_ABS_LO12_NC"},
    {0x11d, "R_AARCH64_LDST32_ABS_LO12_NC"}, {0x11e, "R_AARCH64_LDST64_ABS_LO12_NC"},
    {0x11f, "R_AARCH64_MOVW_PREL_G0"}, {0x120, "R_AARCH64_MOVW_PREL_G0_NC"}, {0x121, "R_AARCH64_MOVW_PREL_G1"},
    {0x122, "R_AARCH64_MOVW_PREL_G1_NC"}, {0x123, "R_AARCH64_MOVW_PREL_G2"},
    {0x124, "R_AARCH64_MOVW_PREL_G2_NC"}, {0x125, "R_AARCH64_MOVW_PREL_G3"},
    {0x12b, "R_AARCH64_LDST128_ABS_LO12_NC"}, {0x12c, "R_AARCH64_MOVW_GOTOFF_G0"},
    {0x12d, "R_AARCH64_MOVW_GOTOFF_G0_NC"}, {0x12e, "R_AARCH64_MOVW_GOTOFF_G1"},
    {0x12f, "R_AARCH64_MOVW_GOTOFF_G1_NC"}, {0x130, "R_AARCH64_MOVW_GOTOFF_G2"},
    {0x131, "R_AARCH64_MOVW_GOTOFF_G2_NC"}, {0x132, "R_AARCH64_MOVW_GOTOFF_G3"}, {0x133, "R_AARCH64_GOTREL64"},
    {0x134, "R_AARCH64_GOTREL32"}, {0x135, "R_AARCH64_GOT_LD_PREL19"}, {0x136, "R_AARCH64_LD64_GOTOFF_LO15"},
    {0x137, "R_AARCH64_ADR_GOT_PAGE"}, {0x138, "R_AARCH64_LD64_GOT_LO12_NC"},
    {0x139, "R_AARCH64_LD64_GOTPAGE_LO15"}, {0x13a, "R_AARCH64_PLT32"}, {0x13b, "R_AARCH64_GOTPCREL32"},
    {0x200, "R_AARCH64_TLSGD_ADR_PREL21"}, {0x201, "R_AARCH64_TLSGD_ADR_PAGE21"},
    {0x202, "R_AARCH64_TLSGD_ADD_LO12_NC"}, {0x203, "R_AARCH64_TLSGD_MOVW_G1"},
    {0x204, "R_AARCH64_TLSGD_MOVW_G0_NC"}, {0x205, "R_AARCH64_TLSLD_ADR_PREL21"},
    {0x206, "R_AARCH64_TLSLD_ADR_PAGE21"}, {0x207, "R_AARCH64_TLSLD_ADD_LO12_NC"},
    {0x208, "R_AARCH64_TLSLD_MOVW_G1"}, {0x209, "R_AARCH64_TLSLD_MOVW_G0_NC"},
    {0x20a, "R_AARCH64_TLSLD_LD_PREL19"}, {0x20b, "R_AARCH64_TLSLD_MOVW_DTPREL_G2"},
    {0x20c, "R_AARCH64_TLSLD_MOVW_DTPREL_G1"}, {0x20d, "R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC"},
    {0x20e, "R_AARCH64_TLSLD_MOVW_DTPREL_G0"}, {0x20f, "R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC"},
    {0x210, "R_AARCH64_TLSLD_ADD_DTPREL_HI12"}, {0x211, "R_AARCH64_TLSLD_ADD_DTPREL_LO12"},
    {0x212, "R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC"}, {0x213, "R_AARCH64_TLSLD_LDST8_DTPREL_LO12"},
    {0x214, "R_AARCH64_TLSLD_LDST8_DTPREL_LO12_NC"}, {0x215, "R_AARCH64_TLSLD_LDST16_DTPREL_LO12"},
    {0x216, "R_AARCH64_TLSLD_LDST16_DTPREL_LO12_NC"}, {0x217, "R_AARCH64_TLSLD_LDST32_DTPREL_LO12"},
    {0x218, "R_AARCH64_TLSLD_LDST32_DTPREL_LO12_NC"}, {0x219, "R_AARCH64_TLSLD_LDST64_DTPREL_LO12"},
    {0x21a, "R_AARCH64_TLSLD_LDST64_DTPREL_LO12_NC"}, {0x21b, "R_AARCH64_TLSIE_MOVW_GOTTPREL_G1"},
    {0x21c, "R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC"}, {0x21d, "R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21"},
    {0x21e, "R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC"}, {0x21f, "R_AARCH64_TLSIE_LD_GOTTPREL_PREL19"},
    {0x220, "R_AARCH64_TLSLE_MOVW_TPREL_G2"}, {0x221, "R_AARCH64_TLSLE_MOVW_TPREL_G1"},
    {0x222, "R_AARCH64_TLSLE_MOVW_TPREL_G1_NC"}, {0x223, "R_AARCH64_TLSLE_MOVW_TPREL_G0"},
    {0x224, "R_AARCH64_TLSLE_MOVW_TPREL_G0_NC"}, {0x225, "R_AARCH64_TLSLE_ADD_TPREL_HI12"},
    {0x226, "R_AARCH64_TLSLE_ADD_TPREL_LO12"}, {0x227, "R_AARCH64_TLSLE_ADD_TPREL_LO12_NC"},
    {0x228, "R_AARCH64_TLSLE_LDST8_TPREL_LO12"}, {0x229, "R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC"},
    {0x22a, "R_AARCH64_TLSLE_LDST16_TPREL_LO12"}, {0x22b, "R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC"},
    {0x22c, "R_AARCH64_TLSLE_LDST32_TPREL_LO12"}, {0x22d, "R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC"},
    {0x22e, "R_AARCH64_TLSLE_LDST64_TPREL_LO12"}, {0x22f, "R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC"},
    {0x230, "R_AARCH64_TLSDESC_LD_PREL19"}, {0x231, "R_AARCH64_TLSDESC_ADR_PREL21"},
    {0x232, "R_AARCH64_TLSDESC_ADR_PAGE21"}, {0x233, "R_AARCH64_TLSDESC_LD64_LO12"},
    {0x234, "R_AARCH64_TLSDESC_ADD_LO12"}, {0x235, "R_AARCH64_TLSDESC_OFF_G1"},
    {0x236, "R_AARCH64_TLSDESC_OFF_G0_NC"}, {0x237, "R_AARCH64_TLSDESC_LDR"}, {0x238, "R_AARCH64_TLSDESC_ADD"},
    {0x239, "R_AARCH64_TLSDESC_CALL"}, {0x23a, "R_AARCH64_TLSLE_LDST128_TPREL_LO12"},
    {0x23b, "R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC"}, {0x23c, "R_AARCH64_TLSLD_LDST128_DTPREL_LO12"},
    {0x23d, "R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC"}, {0x400, "R_AARCH64_COPY"},
    {0x401, "R_AARCH64_GLOB_DAT"}, {0x402, "R_AARCH64_JUMP_SLOT"}, {0x403, "R_AARCH64_RELATIVE"},
    {0x404, "R_AARCH64_TLS_DTPMOD64"}, {0x405, "R_AARCH64_TLS_DTPREL64"}, {0x406, "R_AARCH64_TLS_TPREL64"},
    {0x407, "R_AARCH64_TLSDESC"}, {0x408, "R_AARCH64_IRELATIVE"},
};

std::string relocation_name(std::uint32_t type) {
    for (const auto& [value, name] : relocation_names) {
        if (value == type) return name;
    }
    return "relocation type " + std::to_string(type);
}

std::string hex(std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%#llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::uint32_t read32(std::span<const std::uint8_t> bytes, std::uint64_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | static_cast<std::uint32_t>(bytes[offset + 1]) << 8U |
           static_cast<std::uint32_t>(bytes[offset + 2]) << 16U | static_cast<std::uint32_t>(bytes[offset + 3]) << 24U;
}

void write_bytes(std::span<std::uint8_t> bytes, std::uint64_t offset, std::uint64_t value, unsigned size) {
    for (unsigned index = 0; index < size; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(value >> (8 * index));
    }
}

// Page(expr), the 4 KiB page that adrp computes.
std::uint64_t page(std::uint64_t value) { return value & ~std::uint64_t{0xfff}; }

// The immediate of adr and adrp: immlo in bits 29-30, immhi in 5-23.
std::uint32_t adr_immediate(std::uint32_t word, std::uint64_t value) {
    constexpr std::uint32_t mask = (3U << 29U) | (0x7ffffU << 5U);
    const auto field = static_cast<std::uint32_t>(((value & 3U) << 29U) | (((value >> 2U) & 0x7ffffU) << 5U));
    return (word & ~mask) | field;
}

std::uint32_t imm12(std::uint32_t word, std::uint64_t value) {
    return (word & ~(0xfffU << 10U)) | (static_cast<std::uint32_t>(value & 0xfffU) << 10U);
}

// A move-wide immediate; a signed relocation turns movz and movn into each
// other for the sign of the value (movk keeps its opcode).
std::uint32_t move_wide(std::uint32_t word, std::uint64_t value, bool signed_value) {
    auto immediate = static_cast<std::uint32_t>(value) & 0x1ffffU;
    if (signed_value && (word & (1U << 29U)) == 0) {
        if ((immediate & 0x10000U) != 0) {
            immediate ^= 0xffffU;
            word &= ~(1U << 30U);
        } else {
            word |= 1U << 30U;
        }
    }
    return (word & ~(0xffffU << 5U)) | ((immediate & 0xffffU) << 5U);
}

bool got_relocation(std::uint32_t type) {
    return type == r_aarch64_adr_got_page || type == r_aarch64_ld64_got_lo12_nc;
}

bool branch26(std::uint32_t type) { return type == r_aarch64_call26 || type == r_aarch64_jump26; }

// B and BL reach [-128 MiB, 128 MiB - 4].
bool branch_reaches(std::uint64_t target, std::uint64_t place) {
    const auto distance = static_cast<std::int64_t>(target - place);
    return distance >= -(std::int64_t{1} << 27) && distance < (std::int64_t{1} << 27);
}

// adrp reaches 4 GiB of pages in each direction.
bool page_reaches(std::uint64_t target, std::uint64_t place) {
    const auto distance = static_cast<std::int64_t>(page(target) - page(place));
    return distance >= -(std::int64_t{1} << 32) && distance < (std::int64_t{1} << 32);
}

// A relocation's target by identity: a global symbol, or a file's local
// symbol by index; and the addend.
struct Destination {
    const void* symbol{};
    std::uint32_t index{};
    std::int64_t addend{};
    auto operator<=>(const Destination&) const = default;
};

Destination destination(const InputSection& section, const assembler::Relocation& relocation) {
    const auto& file = *section.file;
    const auto index = relocation.symbol.value;
    if (index < file.symbols.size() && file.symbols[index].global) {
        return {file.symbols[index].global, 0, relocation.addend};
    }
    return {&file, index, relocation.addend};
}

// S + A, with a merged section's offset mapped.
std::uint64_t target_address(const linker::RelocationContext& context, const linker::SymbolValue& symbol,
                             std::int64_t addend) {
    const auto a = static_cast<std::uint64_t>(addend);
    return symbol.removed ? a : symbol.merge ? context.merged(*symbol.merge, a) : symbol.value + a;
}

// Whether a symbol's address moves with a position-independent image.
bool moves(const linker::SymbolValue& symbol) {
    return !symbol.undefined && !symbol.absolute && !symbol.removed;
}

// The eight-byte aligned slots of the adrp (12-byte) and long (24-byte)
// veneers.
constexpr std::uint64_t veneer_slot_adrp = 16;
constexpr std::uint64_t veneer_slot_long = 24;
// GNU ld's stub group size: sections within 127 MiB share a stub section.
constexpr std::uint64_t stub_group_size = 127U << 20U;

class Aarch64Linker final : public linker::Target {
public:
    bool add_object(const InputFile& file, Diagnostics& diagnostics) override {
        if (file.raw) return true;
        if (!file.object.elf64 || file.object.big_endian) {
            diagnostics.command_error(file.name() + ": only little-endian ELF64 AArch64 objects are supported");
            return false;
        }
        return true;
    }

    [[nodiscard]] std::uint32_t output_flags() const override { return 0; }
    // GNU ld's and ld.lld's maximum page size for AArch64.
    [[nodiscard]] std::uint64_t page_size() const override { return 0x10000; }
    [[nodiscard]] std::string_view common_section(std::uint32_t /*shndx*/) const override { return {}; }
    [[nodiscard]] bool undefined_index(std::uint32_t /*shndx*/) const override { return false; }
    void prune(InputSection& /*section*/, const std::function<bool(std::uint32_t)>& /*foreign*/) const override {}
    void merge_sections(std::span<InputSection* const> /*sections*/, Diagnostics& /*diagnostics*/) override {}
    void finish_sections(std::optional<std::uint64_t> /*gp*/) override {}
    [[nodiscard]] std::vector<std::pair<std::uint32_t, OutputSection*>> extra_segments(
        std::span<OutputSection* const> /*sections*/) const override {
        return {};
    }
    void relocate(const linker::RelocationContext& context, std::span<std::uint8_t> bytes) const override;

    std::vector<std::unique_ptr<InputSection>> create_sections(std::span<InputSection* const> sections) override {
        for (auto* section : sections) {
            for (const auto& relocation : section->relocations) {
                if (!got_relocation(relocation.type)) continue;
                // A .got in the first file that needs one.
                auto got = std::make_unique<InputSection>();
                got->file = section->file;
                got->name = ".got";
                got->flags = elf::shf_alloc | elf::shf_write;
                got->alignment = 8;
                got->entry_size = 8;
                got_ = got.get();
                std::vector<std::unique_ptr<InputSection>> result;
                result.push_back(std::move(got));
                return result;
            }
        }
        return {};
    }

    [[nodiscard]] std::vector<std::pair<std::string, InputSection*>> created_symbols() const override {
        if (!got_) return {};
        return {{"_GLOBAL_OFFSET_TABLE_", got_}};
    }

    // A GOT entry for each symbol and addend, in ld.lld's order: global
    // symbols in the order the inputs first name them, then each file's
    // local symbols by index (addends of one symbol by first use).
    void size_sections(std::span<InputSection* const> sections) override {
        if (!got_) return;
        std::vector<const InputFile*> files;
        std::unordered_map<const void*, std::size_t> ranks;
        for (const auto* section : sections) {
            if (std::find(files.begin(), files.end(), section->file) != files.end()) continue;
            files.push_back(section->file);
            for (const auto& entry : section->file->symbols) {
                if (entry.global) ranks.emplace(entry.global, ranks.size());
            }
        }
        struct Use {
            Destination key;
            GotEntry entry;
            std::size_t first{};
        };
        std::vector<Use> uses;
        std::map<Destination, std::size_t> seen;
        for (auto* section : sections) {
            for (const auto& relocation : section->relocations) {
                if (!got_relocation(relocation.type)) continue;
                const auto key = destination(*section, relocation);
                if (seen.emplace(key, uses.size()).second) {
                    uses.push_back({key, {section, relocation.symbol.value, relocation.addend}, uses.size()});
                }
            }
        }
        const auto rank = [&](const Use& use) {
            const auto global = ranks.find(use.key.symbol);
            if (global != ranks.end()) return std::make_tuple(0, global->second, std::size_t{0}, use.first);
            const auto file = std::find(files.begin(), files.end(), use.key.symbol) - files.begin();
            return std::make_tuple(1, static_cast<std::size_t>(file), std::size_t{use.key.index}, use.first);
        };
        std::sort(uses.begin(), uses.end(), [&](const Use& a, const Use& b) { return rank(a) < rank(b); });
        got_entries_.clear();
        got_index_.clear();
        for (const auto& use : uses) {
            got_index_.emplace(use.key, got_entries_.size());
            got_entries_.push_back(use.entry);
        }
        got_->size = 8 * got_entries_.size();
        got_->data.assign(got_->size, 0);
    }

    bool add_thunks(std::span<OutputSection* const> outputs, const linker::ContextFactory& context,
                    const linker::Insert& insert) override;

    [[nodiscard]] std::uint32_t relative_relocation() const override { return r_aarch64_relative; }

    [[nodiscard]] std::size_t relative_relocations(std::span<InputSection* const> sections,
                                                   const linker::ContextFactory& context) const override {
        std::size_t count = 0;
        for (auto* section : sections) {
            // relocate() sees the sections that have contents.
            if (!section->alloc() || section->relocations.empty() || section->nobits() ||
                (section->output && section->output->nobits())) {
                continue;
            }
            const auto view = context(*section);
            for (const auto& relocation : section->relocations) {
                if (relocation.type == r_aarch64_abs64 && moves(view->symbol(relocation.symbol.value))) ++count;
            }
        }
        for (const auto& entry : got_entries_) {
            if (moves(context(*entry.section)->symbol(entry.symbol))) ++count;
        }
        return count;
    }

    void fill_sections(std::span<InputSection* const> sections, const linker::ContextFactory& context) override;

    [[nodiscard]] bool sections_end_locals(const linker::OutputFormat& /*format*/) const override { return false; }
    void finish_output_section(std::string_view /*name*/, const linker::OutputFormat& /*format*/,
                               std::uint32_t& /*type*/, std::uint64_t& /*flags*/,
                               std::uint64_t& /*entry_size*/) const override {}
    [[nodiscard]] std::string default_script(linker::OutputKind kind) const override;
    // GNU ld's AArch64 default: no executable stack without a note.
    [[nodiscard]] bool default_executable_stack() const override { return false; }
    [[nodiscard]] bool local_label(std::string_view /*name*/) const override { return false; }

    [[nodiscard]] std::vector<std::int64_t> addends(const InputSection& section) const override {
        std::vector<std::int64_t> result;
        result.reserve(section.relocations.size());
        for (const auto& relocation : section.relocations) result.push_back(relocation.addend);
        return result;
    }

    bool store_addend(std::uint32_t /*type*/, std::int64_t /*addend*/,
                      std::span<std::uint8_t> /*field*/) const override {
        return false;
    }

private:
    struct GotEntry {
        // A relocation that names the entry's symbol, for its value.
        const InputSection* section{};
        std::uint32_t symbol{};
        std::int64_t addend{};
    };

    struct Veneer {
        // A branch to the veneer's target, for the target's address.
        const InputSection* section{};
        std::uint32_t symbol{};
        std::int64_t addend{};
        std::string name;
        // The long form, once a stub cannot reach its target by adrp, stays.
        bool long_form{};
        std::uint64_t offset{};
    };

    // GNU ld's stub section of a group of code sections, placed after the
    // group's last section.
    struct Stub {
        InputSection* after{};
        InputSection* section{};
        std::vector<Veneer> veneers;
        std::map<Destination, std::size_t> index;
    };

    void group_sections(std::span<OutputSection* const> outputs);
    // The address a veneer branches to.
    [[nodiscard]] static std::uint64_t veneer_target(const linker::ContextFactory& context, const Veneer& veneer) {
        const auto view = context(*veneer.section);
        return target_address(*view, view->symbol(veneer.symbol), veneer.addend);
    }
    // The veneer a branch takes, if its group has one for its target.
    [[nodiscard]] std::optional<std::uint64_t> veneer_address(const linker::RelocationContext& context,
                                                              const assembler::Relocation& relocation) const {
        const auto& section = context.section();
        const auto group = groups_.find(&section);
        if (group == groups_.end() || !group->second->section) return std::nullopt;
        const auto& stub = *group->second;
        const auto found = stub.index.find(destination(section, relocation));
        if (found == stub.index.end()) return std::nullopt;
        return context.merged(*stub.section, stub.veneers[found->second].offset);
    }

    InputSection* got_{};
    std::vector<GotEntry> got_entries_;
    std::map<Destination, std::size_t> got_index_;
    bool grouped_{};
    std::vector<std::unique_ptr<Stub>> stubs_;
    std::unordered_map<const InputSection*, Stub*> groups_;
};

void Aarch64Linker::group_sections(std::span<OutputSection* const> outputs) {
    // GNU ld's group_sections: the code sections of an output section whose
    // ends lie within the group size of the first one's start share the
    // stub section after the last of them, as do the sections that end
    // within the group size after that.
    for (auto* output : outputs) {
        std::vector<InputSection*> code;
        for (auto* section : output->sections) {
            if ((section->flags & elf::shf_execinstr) != 0) code.push_back(section);
        }
        std::size_t head = 0;
        while (head < code.size()) {
            const auto start = code[head]->offset;
            auto last = head;
            while (last + 1 < code.size() && code[last + 1]->offset + code[last + 1]->size - start < stub_group_size) {
                ++last;
            }
            auto stub = std::make_unique<Stub>();
            stub->after = code[last];
            for (auto index = head; index <= last; ++index) groups_[code[index]] = stub.get();
            const auto stubs_start = code[last]->offset + code[last]->size;
            auto next = last + 1;
            while (next < code.size() && code[next]->offset + code[next]->size - stubs_start < stub_group_size) {
                groups_[code[next++]] = stub.get();
            }
            stubs_.push_back(std::move(stub));
            head = next;
        }
    }
}

bool Aarch64Linker::add_thunks(std::span<OutputSection* const> outputs, const linker::ContextFactory& context,
                               const linker::Insert& insert) {
    if (!grouped_) {
        group_sections(outputs);
        grouped_ = true;
    }
    bool changed = false;
    // A veneer for each B or BL target that its branch does not reach, in
    // the branch's group; veneers stay once created.
    for (auto* output : outputs) {
        for (auto* section : output->sections) {
            if (section->relocations.empty()) continue;
            const auto group = groups_.find(section);
            if (group == groups_.end()) continue;
            auto& stub = *group->second;
            const auto view = context(*section);
            const auto& file = *section->file;
            for (const auto& relocation : section->relocations) {
                if (!branch26(relocation.type)) continue;
                const auto symbol = view->symbol(relocation.symbol.value);
                if (symbol.undefined || symbol.removed) continue;
                // GNU ld: a branch within its section to a symbol other than
                // a function needs no veneer.
                const auto index = relocation.symbol.value;
                if (index < file.symbols.size()) {
                    const auto& entry = file.symbols[index];
                    const auto type = entry.global ? entry.global->type : entry.type;
                    const auto* home = entry.global ? entry.global->section
                                       : entry.shndx < file.sections.size() ? file.sections[entry.shndx].get()
                                                                            : nullptr;
                    if (type != elf::stt_func && home == section) continue;
                }
                if (branch_reaches(target_address(*view, symbol, relocation.addend), view->place(relocation.offset))) {
                    continue;
                }
                const auto key = destination(*section, relocation);
                if (stub.index.contains(key)) continue;
                // GNU ld names a veneer after its symbol, or the branch's
                // section when the symbol has no name.
                std::string name = symbol.name.empty() ? section->name : std::string(symbol.name);
                stub.index.emplace(key, stub.veneers.size());
                stub.veneers.push_back({section, index, relocation.addend, "__" + name + "_veneer"});
                changed = true;
            }
        }
    }
    // Each veneer takes the adrp form while its target lies within 4 GiB of
    // pages, else the long form; the section starts with a branch over the
    // veneers and a nop (GNU ld), and veneers are eight-byte aligned.
    for (auto& stub : stubs_) {
        if (stub->veneers.empty()) continue;
        if (!stub->section) {
            auto section = std::make_unique<InputSection>();
            section->file = stub->after->file;
            section->name = stub->after->name + ".stub";
            section->flags = elf::shf_alloc | elf::shf_execinstr;
            section->alignment = 8;
            stub->section = section.get();
            insert(*stub->after, std::move(section));
            changed = true;
        }
        // Before its first layout, the section follows its group.
        const auto base = stub->section->output
                              ? context(*stub->section)->place(0)
                              : (context(*stub->after)->place(stub->after->size) + 7) & ~std::uint64_t{7};
        std::uint64_t offset = 8;
        for (auto& veneer : stub->veneers) {
            if (!veneer.long_form && !page_reaches(veneer_target(context, veneer), base + offset)) {
                veneer.long_form = true;
                changed = true;
            }
            veneer.offset = offset;
            offset += veneer.long_form ? veneer_slot_long : veneer_slot_adrp;
        }
        if (stub->section->size != offset) {
            stub->section->size = offset;
            stub->section->data.assign(offset, 0);
            changed = true;
        }
    }
    return changed;
}

void Aarch64Linker::fill_sections(std::span<InputSection* const> /*sections*/, const linker::ContextFactory& context) {
    if (got_) {
        const auto got_view = context(*got_);
        for (std::size_t index = 0; index < got_entries_.size(); ++index) {
            const auto& entry = got_entries_[index];
            const auto view = context(*entry.section);
            const auto symbol = view->symbol(entry.symbol);
            const auto value = symbol.undefined ? 0 : target_address(*view, symbol, entry.addend);
            write_bytes(got_->data, 8 * index, value, 8);
            if (got_view->position_independent() && moves(symbol)) {
                got_view->relative(8 * index, value, "the GOT entry of '" + std::string(symbol.name) + "'");
            }
        }
    }
    for (auto& stub : stubs_) {
        if (!stub->section) continue;
        auto& section = *stub->section;
        auto& data = section.data;
        const auto base = context(section)->place(0);
        write_bytes(data, 0, 0x14000000U | ((section.size >> 2U) & 0x3ffffffU), 4);
        write_bytes(data, 4, 0xd503201fU, 4);
        auto& file = *section.file;
        std::uint32_t shndx = 0;
        while (shndx < file.sections.size() && file.sections[shndx].get() != &section) ++shndx;
        const auto local = [&](std::string name, std::uint64_t value, std::uint64_t size, std::uint8_t type) {
            file.symbols.push_back({std::move(name), value, size, type, elf::stb_local, 0, shndx, nullptr});
        };
        local("$x", 0, 0, elf::stt_notype);
        for (const auto& veneer : stub->veneers) {
            const auto target = veneer_target(context, veneer);
            const auto place = base + veneer.offset;
            const auto at = veneer.offset;
            if (veneer.long_form) {
                // ldr x16, 1f; adr x17, #0; add x16, x16, x17; br x16;
                // 1: .xword target - (the adr)
                write_bytes(data, at, 0x58000090U, 4);
                write_bytes(data, at + 4, 0x10000011U, 4);
                write_bytes(data, at + 8, 0x8b110210U, 4);
                write_bytes(data, at + 12, 0xd61f0200U, 4);
                write_bytes(data, at + 16, target - (place + 4), 8);
            } else {
                // adrp x16, target; add x16, x16, :lo12:target; br x16
                write_bytes(data, at, adr_immediate(0x90000010U, (page(target) - page(place)) >> 12U), 4);
                write_bytes(data, at + 4, imm12(0x91000210U, target), 4);
                write_bytes(data, at + 8, 0xd61f0200U, 4);
            }
            local(veneer.name, at, veneer.long_form ? 24 : 12, elf::stt_func);
            local("$x", at, 0, elf::stt_notype);
            if (veneer.long_form) local("$d", at + 16, 0, elf::stt_notype);
        }
    }
}

void Aarch64Linker::relocate(const linker::RelocationContext& context, std::span<std::uint8_t> bytes) const {
    const auto& section = context.section();
    // Debugging and unwind tables may refer to removed code.
    const bool tombstone = !section.alloc() || section.name == ".eh_frame";
    const bool pie = context.position_independent();
    if (!section.rela && !section.relocations.empty()) {
        context.error(0, "REL relocations are not supported for AArch64");
        return;
    }
    for (const auto& relocation : section.relocations) {
        const auto type = relocation.type;
        // 256 is the withdrawn second number of R_AARCH64_NONE.
        if (type == r_aarch64_none || type == 0x100) continue;
        const auto symbol = context.symbol(relocation.symbol.value);
        const auto addend = relocation.addend;
        const auto offset = relocation.offset;
        std::string name = symbol.name.empty() ? std::string("<absolute>") : std::string(symbol.name);
        if (symbol.section && addend != 0) {
            name += (addend < 0 ? "-" : "+") + hex(addend < 0 ? -static_cast<std::uint64_t>(addend)
                                                               : static_cast<std::uint64_t>(addend));
        }
        const auto what = relocation_name(type) + " against '" + name + "'";
        unsigned size = 4;
        if (type == r_aarch64_abs64 || type == r_aarch64_prel64) size = 8;
        if (type == r_aarch64_abs16 || type == r_aarch64_prel16) size = 2;
        if (offset > bytes.size() || size > bytes.size() - offset) {
            context.error(offset, relocation_name(type) + " lies outside its section");
            continue;
        }
        if (symbol.removed && !tombstone) {
            context.error(offset, relocation_name(type) + " refers to '" + name +
                                      "', which is in a discarded section");
            continue;
        }
        const std::uint64_t p = context.place(offset);
        const std::uint64_t s_a = symbol.undefined ? static_cast<std::uint64_t>(addend)
                                                   : target_address(context, symbol, addend);
        const auto word = size == 4 ? read32(bytes, offset) : 0U;
        const auto store = [&](std::uint32_t value) { write_bytes(bytes, offset, value, 4); };
        const auto out_of_range = [&](std::uint64_t value, std::int64_t low, std::int64_t high, bool is_signed) {
            const auto text = is_signed ? std::to_string(static_cast<std::int64_t>(value)) : std::to_string(value);
            context.error(offset, what + " out of range: " + text + " is not in [" + std::to_string(low) + ", " +
                                      std::to_string(high) + "]");
        };
        // [-2^(bits-1), 2^(bits-1)).
        const auto check_signed = [&](std::uint64_t value, unsigned bits) {
            const auto limit = std::int64_t{1} << (bits - 1);
            const auto number = static_cast<std::int64_t>(value);
            if (number >= -limit && number < limit) return true;
            out_of_range(value, -limit, limit - 1, true);
            return false;
        };
        // [0, 2^bits).
        const auto check_unsigned = [&](std::uint64_t value, unsigned bits) {
            if ((value >> bits) == 0) return true;
            out_of_range(value, 0, static_cast<std::int64_t>((std::uint64_t{1} << bits) - 1), false);
            return false;
        };
        // [-2^(bits-1), 2^bits): a value read as signed or unsigned.
        const auto check_either = [&](std::uint64_t value, unsigned bits) {
            const auto number = static_cast<std::int64_t>(value);
            const auto low = -(std::int64_t{1} << (bits - 1));
            const auto high = std::int64_t{1} << bits;
            if (number >= low && number < high) return true;
            out_of_range(value, low, high - 1, true);
            return false;
        };
        const auto check_aligned = [&](std::uint64_t value, std::uint64_t alignment) {
            if ((value & (alignment - 1)) == 0) return true;
            context.error(offset, what + ": " + hex(value) + " is not a multiple of " + std::to_string(alignment));
            return false;
        };
        // Absolute addresses of what moves need a dynamic relocation, which
        // only R_AARCH64_ABS64 has; PC-relative ones cannot reach what does
        // not move.
        const auto absolute_in_pie = [&] {
            if (!pie || !section.alloc() || !moves(symbol)) return false;
            context.error(offset, what + " cannot be used in a position-independent executable; only "
                                         "R_AARCH64_ABS64 gets a dynamic relocation");
            return true;
        };
        const auto relative_to_absolute = [&] {
            if (!pie || !section.alloc() || !symbol.absolute) return false;
            context.error(offset, what + " refers to an absolute symbol, which a position-independent "
                                         "executable cannot reach PC-relatively");
            return true;
        };
        // The TP-relative offset of a TLS symbol: the TLS block follows a
        // 16-byte control block, at the block's alignment (variant 1).
        const auto tprel = [&]() -> std::optional<std::uint64_t> {
            const auto base = context.tls_base();
            if (!base) {
                context.error(offset, what + " needs a TLS segment, which the link lacks");
                return std::nullopt;
            }
            if (symbol.undefined) {
                context.error(offset, what + " refers to an undefined TLS symbol");
                return std::nullopt;
            }
            const auto alignment = context.tls_alignment();
            return s_a - *base + ((16 + alignment - 1) & ~(alignment - 1));
        };
        // The scaled low 12 bits of a load or store of `scale` bytes.
        const auto low12 = [&](std::uint64_t value, std::uint64_t scale) {
            if (!check_aligned(value, scale)) return;
            store(imm12(word, (value & 0xfffU) / scale));
        };
        switch (type) {
        case r_aarch64_abs64:
            write_bytes(bytes, offset, s_a, 8);
            if (pie && section.alloc() && moves(symbol)) context.relative(offset, s_a, what);
            break;
        case r_aarch64_abs32:
            if (absolute_in_pie() || !check_either(s_a, 32)) continue;
            write_bytes(bytes, offset, s_a, 4);
            break;
        case r_aarch64_abs16:
            if (absolute_in_pie() || !check_either(s_a, 16)) continue;
            write_bytes(bytes, offset, s_a, 2);
            break;
        case r_aarch64_prel64:
        case r_aarch64_prel32:
        case r_aarch64_prel16: {
            if (relative_to_absolute()) continue;
            const auto value = symbol.undefined ? static_cast<std::uint64_t>(addend) : s_a - p;
            if (size < 8 && !check_signed(value, size * 8)) continue;
            write_bytes(bytes, offset, value, size);
            break;
        }
        case r_aarch64_adr_prel_lo21: {
            if (relative_to_absolute()) continue;
            const auto value = symbol.undefined ? static_cast<std::uint64_t>(addend) : s_a - p;
            if (!check_signed(value, 21)) continue;
            store(adr_immediate(word, value));
            break;
        }
        case r_aarch64_adr_prel_pg_hi21:
        case r_aarch64_adr_prel_pg_hi21_nc: {
            if (relative_to_absolute()) continue;
            const auto target = symbol.undefined ? p + static_cast<std::uint64_t>(addend) : s_a;
            const auto value = page(target) - page(p);
            if (type == r_aarch64_adr_prel_pg_hi21 && !check_signed(value, 33)) continue;
            store(adr_immediate(word, value >> 12U));
            break;
        }
        case r_aarch64_add_abs_lo12_nc:
            store(imm12(word, s_a));
            break;
        case r_aarch64_ldst8_abs_lo12_nc: low12(s_a, 1); break;
        case r_aarch64_ldst16_abs_lo12_nc: low12(s_a, 2); break;
        case r_aarch64_ldst32_abs_lo12_nc: low12(s_a, 4); break;
        case r_aarch64_ldst64_abs_lo12_nc: low12(s_a, 8); break;
        case r_aarch64_ldst128_abs_lo12_nc: low12(s_a, 16); break;
        case r_aarch64_call26:
        case r_aarch64_jump26:
        case r_aarch64_condbr19:
        case r_aarch64_tstbr14:
        case r_aarch64_ld_prel_lo19: {
            if (relative_to_absolute()) continue;
            const bool branch = type != r_aarch64_ld_prel_lo19;
            // A branch to an undefined weak symbol goes to the next
            // instruction (ld.lld).
            auto target = symbol.undefined ? p + (branch ? 4U : 0U) + static_cast<std::uint64_t>(addend) : s_a;
            if (branch26(type) && !branch_reaches(target, p)) {
                if (const auto veneer = veneer_address(context, relocation)) target = *veneer;
            }
            const auto value = target - p;
            if (!check_aligned(value, 4)) continue;
            if (branch26(type)) {
                if (!check_signed(value, 28)) continue;
                store((word & ~0x3ffffffU) | (static_cast<std::uint32_t>(value >> 2U) & 0x3ffffffU));
            } else if (type == r_aarch64_tstbr14) {
                if (!check_signed(value, 16)) continue;
                store((word & ~(0x3fffU << 5U)) | ((static_cast<std::uint32_t>(value >> 2U) & 0x3fffU) << 5U));
            } else {
                if (!check_signed(value, 21)) continue;
                store((word & ~(0x7ffffU << 5U)) | ((static_cast<std::uint32_t>(value >> 2U) & 0x7ffffU) << 5U));
            }
            break;
        }
        case r_aarch64_movw_uabs_g0:
        case r_aarch64_movw_uabs_g0_nc:
        case r_aarch64_movw_uabs_g1:
        case r_aarch64_movw_uabs_g1_nc:
        case r_aarch64_movw_uabs_g2:
        case r_aarch64_movw_uabs_g2_nc:
        case r_aarch64_movw_uabs_g3: {
            if (absolute_in_pie()) continue;
            const unsigned group = (type - r_aarch64_movw_uabs_g0) / 2;
            const bool checked = type == r_aarch64_movw_uabs_g0 || type == r_aarch64_movw_uabs_g1 ||
                                 type == r_aarch64_movw_uabs_g2;
            if (checked && !check_unsigned(s_a, 16 * (group + 1))) continue;
            store(move_wide(word, s_a >> (16 * group), false));
            break;
        }
        case r_aarch64_movw_sabs_g0:
        case r_aarch64_movw_sabs_g1:
        case r_aarch64_movw_sabs_g2: {
            if (absolute_in_pie()) continue;
            const unsigned group = type - r_aarch64_movw_sabs_g0;
            if (!check_signed(s_a, 16 * (group + 1) + 1)) continue;
            store(move_wide(word, s_a >> (16 * group), true));
            break;
        }
        case r_aarch64_adr_got_page:
        case r_aarch64_ld64_got_lo12_nc: {
            const auto found = got_index_.find(destination(section, relocation));
            if (!got_ || found == got_index_.end()) {
                context.error(offset, "internal error: " + what + " has no GOT entry");
                continue;
            }
            const auto entry = context.merged(*got_, 8 * found->second);
            if (type == r_aarch64_adr_got_page) {
                const auto value = page(entry) - page(p);
                if (!check_signed(value, 33)) continue;
                store(adr_immediate(word, value >> 12U));
            } else {
                low12(entry, 8);
            }
            break;
        }
        case r_aarch64_tlsle_movw_tprel_g2:
        case r_aarch64_tlsle_movw_tprel_g1:
        case r_aarch64_tlsle_movw_tprel_g1_nc:
        case r_aarch64_tlsle_movw_tprel_g0:
        case r_aarch64_tlsle_movw_tprel_g0_nc: {
            const auto value = tprel();
            if (!value) continue;
            unsigned group = 0;
            bool checked = true;
            switch (type) {
            case r_aarch64_tlsle_movw_tprel_g2: group = 2; break;
            case r_aarch64_tlsle_movw_tprel_g1: group = 1; break;
            case r_aarch64_tlsle_movw_tprel_g1_nc: group = 1; checked = false; break;
            case r_aarch64_tlsle_movw_tprel_g0_nc: checked = false; break;
            default: break;
            }
            if (checked && !check_signed(*value, 16 * (group + 1) + 1)) continue;
            store(move_wide(word, *value >> (16 * group), true));
            break;
        }
        case r_aarch64_tlsle_add_tprel_hi12: {
            const auto value = tprel();
            if (!value || !check_unsigned(*value, 24)) continue;
            store(imm12(word, *value >> 12U));
            break;
        }
        case r_aarch64_tlsle_add_tprel_lo12:
        case r_aarch64_tlsle_add_tprel_lo12_nc: {
            const auto value = tprel();
            if (!value) continue;
            if (type == r_aarch64_tlsle_add_tprel_lo12 && !check_unsigned(*value, 12)) continue;
            store(imm12(word, *value));
            break;
        }
        case r_aarch64_tlsle_ldst8_tprel_lo12:
        case r_aarch64_tlsle_ldst8_tprel_lo12_nc:
        case r_aarch64_tlsle_ldst16_tprel_lo12:
        case r_aarch64_tlsle_ldst16_tprel_lo12_nc:
        case r_aarch64_tlsle_ldst32_tprel_lo12:
        case r_aarch64_tlsle_ldst32_tprel_lo12_nc:
        case r_aarch64_tlsle_ldst64_tprel_lo12:
        case r_aarch64_tlsle_ldst64_tprel_lo12_nc:
        case r_aarch64_tlsle_ldst128_tprel_lo12:
        case r_aarch64_tlsle_ldst128_tprel_lo12_nc: {
            const auto value = tprel();
            if (!value) continue;
            const bool wide = type == r_aarch64_tlsle_ldst128_tprel_lo12 ||
                              type == r_aarch64_tlsle_ldst128_tprel_lo12_nc;
            const auto scale = wide ? 16U : 1U << ((type - r_aarch64_tlsle_ldst8_tprel_lo12) / 2);
            const bool checked = wide ? type == r_aarch64_tlsle_ldst128_tprel_lo12
                                      : (type - r_aarch64_tlsle_ldst8_tprel_lo12) % 2 == 0;
            if (checked && !check_unsigned(*value, 12)) continue;
            low12(*value, scale);
            break;
        }
        // Initial exec relaxes to local exec in an executable, as in GNU ld
        // and ld.lld: adrp becomes movz of bits 16-31 and the load movk of
        // bits 0-15, each into its own destination register.
        case r_aarch64_tlsie_adr_gottprel_page21:
        case r_aarch64_tlsie_ld64_gottprel_lo12_nc: {
            const auto value = tprel();
            if (!value || !check_unsigned(*value, 32)) continue;
            const auto destination_register = word & 0x1fU;
            if (type == r_aarch64_tlsie_adr_gottprel_page21) {
                store(0xd2a00000U | destination_register | static_cast<std::uint32_t>(((*value >> 16U) & 0xffffU) << 5U));
            } else {
                store(0xf2800000U | destination_register | static_cast<std::uint32_t>((*value & 0xffffU) << 5U));
            }
            break;
        }
        default:
            context.error(offset, "unsupported " + what);
            break;
        }
    }
}

std::string Aarch64Linker::default_script(linker::OutputKind kind) const {
    if (kind == linker::OutputKind::Relocatable) {
        // GNU ld's relocatable layout for AArch64.
        return "SECTIONS\n{\n"
               "  .note.gnu.build-id : { *(.note.gnu.build-id) }\n"
               "  .interp : { *(.interp) }\n"
               "  .hash : { *(.hash) }\n"
               "  .gnu.hash : { *(.gnu.hash) }\n"
               "  .dynsym : { *(.dynsym) }\n"
               "  .dynstr : { *(.dynstr) }\n"
               "  .gnu.version : { *(.gnu.version) }\n"
               "  .gnu.version_d : { *(.gnu.version_d) }\n"
               "  .gnu.version_r : { *(.gnu.version_r) }\n"
               "  .init : { KEEP (*(SORT_NONE(.init))) }\n"
               "  .plt : { *(.plt) }\n"
               "  .text : { *(.text .stub) *(.gnu.warning) }\n"
               "  .fini : { KEEP (*(SORT_NONE(.fini))) }\n"
               "  .rodata : { *(.rodata) }\n"
               "  .rodata1 : { *(.rodata1) }\n"
               "  .eh_frame_hdr : { *(.eh_frame_hdr) }\n"
               "  .eh_frame : { KEEP (*(.eh_frame)) }\n"
               "  .sframe : { KEEP (*(.sframe)) }\n"
               "  .gcc_except_table : { *(.gcc_except_table) }\n"
               "  .gnu_extab : { *(.gnu_extab*) }\n"
               "  .exception_ranges : { *(.exception_ranges) }\n"
               "  .tdata : { *(.tdata) }\n"
               "  .tbss : { *(.tbss) }\n"
               "  .jcr : { KEEP (*(.jcr)) }\n"
               "  .dynamic : { *(.dynamic) }\n"
               "  .got : { *(.got) }\n"
               "  .got.plt : { *(.got.plt) }\n"
               "  .data : { *(.data) }\n"
               "  .data1 : { *(.data1) }\n"
               "  .bss : { *(.bss) }\n"
               "  .stab : { *(.stab) }\n"
               "  .stabstr : { *(.stabstr) }\n"
               "  .stab.excl : { *(.stab.excl) }\n"
               "  .stab.exclstr : { *(.stab.exclstr) }\n"
               "  .stab.index : { *(.stab.index) }\n"
               "  .stab.indexstr : { *(.stab.indexstr) }\n"
               "  .comment : { *(.comment) }\n"
               "  .gnu.build.attributes : { *(.gnu.build.attributes) }\n"
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
               "  .debug_weaknames : { *(.debug_weaknames) }\n"
               "  .debug_funcnames : { *(.debug_funcnames) }\n"
               "  .debug_typenames : { *(.debug_typenames) }\n"
               "  .debug_varnames : { *(.debug_varnames) }\n"
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
               "  .ARM.attributes : { KEEP (*(.ARM.attributes)) KEEP (*(.gnu.attributes)) }\n"
               "  .note.gnu.arm.ident : { KEEP (*(.note.gnu.arm.ident)) }\n"
               "}\n";
    }
    // A subset of GNU ld's aarch64linux script (-pie: from address 0), with
    // the dynamic sections of a position-independent executable.
    const bool pie = kind == linker::OutputKind::PositionIndependent;
    const std::string base = pie ? "0" : "0x400000";
    std::string text = "SECTIONS\n{\n"
                       "  PROVIDE (__executable_start = SEGMENT_START(\"text-segment\", " + base + "));\n"
                       "  . = SEGMENT_START(\"text-segment\", " + base + ") + SIZEOF_HEADERS;\n"
                       "  .note.gnu.build-id : { *(.note.gnu.build-id) }\n"
                       "  .dynsym : { *(.dynsym) }\n"
                       "  .dynstr : { *(.dynstr) }\n"
                       "  .rela.dyn : { *(.rela.dyn) }\n";
    if (!pie) {
        text += "  .rela.plt : { *(.rela.plt) PROVIDE_HIDDEN (__rela_iplt_start = .); *(.rela.iplt)"
                " PROVIDE_HIDDEN (__rela_iplt_end = .); }\n";
    }
    text += "  .init : { KEEP (*(SORT_NONE(.init))) } =0x1f2003d5\n"
            "  .text :\n"
            "  {\n"
            "    *(.text.unlikely .text.*_unlikely .text.unlikely.*)\n"
            "    *(.text.exit .text.exit.*)\n"
            "    *(.text.startup .text.startup.*)\n"
            "    *(.text.hot .text.hot.*)\n"
            "    *(SORT(.text.sorted.*))\n"
            "    *(.text .stub .text.* .gnu.linkonce.t.*)\n"
            "    *(.gnu.warning)\n"
            "  } =0x1f2003d5\n"
            "  .fini : { KEEP (*(SORT_NONE(.fini))) } =0x1f2003d5\n"
            "  PROVIDE (__etext = .);\n"
            "  PROVIDE (_etext = .);\n"
            "  PROVIDE (etext = .);\n"
            "  .rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) }\n"
            "  .rodata1 : { *(.rodata1) }\n"
            "  .eh_frame_hdr : { *(.eh_frame_hdr) *(.eh_frame_entry .eh_frame_entry.*) }\n"
            "  .eh_frame : ONLY_IF_RO { KEEP (*(.eh_frame)) *(.eh_frame.*) }\n"
            "  .gcc_except_table : ONLY_IF_RO { *(.gcc_except_table .gcc_except_table.*) }\n"
            "  .note.gnu.property : { *(.note.gnu.property) }\n"
            "  .note.ABI-tag : { *(.note.ABI-tag) }\n"
            "  . = DATA_SEGMENT_ALIGN (CONSTANT (MAXPAGESIZE), CONSTANT (COMMONPAGESIZE));\n"
            "  .eh_frame : ONLY_IF_RW { KEEP (*(.eh_frame)) *(.eh_frame.*) }\n"
            "  .gcc_except_table : ONLY_IF_RW { *(.gcc_except_table .gcc_except_table.*) }\n"
            "  .tdata : { PROVIDE_HIDDEN (__tdata_start = .); *(.tdata .tdata.* .gnu.linkonce.td.*) }\n"
            "  .tbss : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }\n"
            "  .preinit_array :\n"
            "  {\n"
            "    PROVIDE_HIDDEN (__preinit_array_start = .);\n"
            "    KEEP (*(.preinit_array))\n"
            "    PROVIDE_HIDDEN (__preinit_array_end = .);\n"
            "  }\n"
            "  .init_array :\n"
            "  {\n"
            "    PROVIDE_HIDDEN (__init_array_start = .);\n"
            "    KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))\n"
            "    KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))\n"
            "    PROVIDE_HIDDEN (__init_array_end = .);\n"
            "  }\n"
            "  .fini_array :\n"
            "  {\n"
            "    PROVIDE_HIDDEN (__fini_array_start = .);\n"
            "    KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))\n"
            "    KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))\n"
            "    PROVIDE_HIDDEN (__fini_array_end = .);\n"
            "  }\n"
            "  .data.rel.ro : { *(.data.rel.ro.local* .gnu.linkonce.d.rel.ro.local.*)"
            " *(.data.rel.ro .data.rel.ro.* .gnu.linkonce.d.rel.ro.*) }\n"
            "  .dynamic : { *(.dynamic) }\n"
            "  .got : { *(.got) *(.igot) }\n"
            "  .got.plt : { *(.got.plt) *(.igot.plt) }\n"
            "  .data : { PROVIDE (__data_start = .); *(.data .data.* .gnu.linkonce.d.*) }\n"
            "  .data1 : { *(.data1) }\n"
            "  _edata = .;\n"
            "  PROVIDE (edata = .);\n"
            "  . = ALIGN(ALIGNOF(NEXT_SECTION));\n"
            "  __bss_start = .;\n"
            "  __bss_start__ = .;\n"
            "  .bss :\n"
            "  {\n"
            "    *(.dynbss)\n"
            "    *(.bss .bss.* .gnu.linkonce.b.*)\n"
            "    *(COMMON)\n"
            "    . = ALIGN(. != 0 ? 64 / 8 : 1);\n"
            "  }\n"
            "  _bss_end__ = .;\n"
            "  __bss_end__ = .;\n"
            "  . = ALIGN(64 / 8);\n"
            "  . = SEGMENT_START(\"ldata-segment\", .);\n"
            "  . = ALIGN(64 / 8);\n"
            "  __end__ = .;\n"
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
            "  .ARM.attributes 0 : { KEEP (*(.ARM.attributes)) KEEP (*(.gnu.attributes)) }\n"
            "  .note.gnu.arm.ident 0 : { KEEP (*(.note.gnu.arm.ident)) }\n"
            "  /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.gnu.lto_*) *(.gnu_object_only) }\n"
            "}\n";
    return text;
}

} // namespace

std::unique_ptr<linker::Target> make_linker_target(const assembler::ObjectInfo& /*output*/) {
    return std::make_unique<Aarch64Linker>();
}

} // namespace cross::aarch64
