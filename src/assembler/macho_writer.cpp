// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/macho_writer.hpp"

#include "assembler/elf_writer.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace cross::assembler {
namespace {

constexpr std::uint32_t mh_magic_64 = 0xfeedfacf;
constexpr std::uint32_t mh_object = 1;
constexpr std::uint32_t mh_subsections_via_symbols = 0x2000;
constexpr std::uint32_t lc_segment_64 = 0x19;
constexpr std::uint32_t lc_symtab = 0x2;
constexpr std::uint32_t lc_dysymtab = 0xb;
constexpr std::uint8_t n_undf = 0x0;
constexpr std::uint8_t n_ext = 0x1;
constexpr std::uint8_t n_abs = 0x2;
constexpr std::uint8_t n_sect = 0xe;
constexpr std::uint8_t n_pext = 0x10;
constexpr std::uint16_t n_no_dead_strip = 0x20;
constexpr std::uint16_t n_weak_ref = 0x40;
constexpr std::uint16_t n_weak_def = 0x80;
constexpr std::uint16_t n_alt_entry = 0x200;

std::uint64_t align_to(std::uint64_t value, std::uint64_t alignment) {
    return alignment > 1 ? (value + alignment - 1) / alignment * alignment : value;
}

// Sections with file contents in creation order, then zero-fill sections.
std::vector<std::size_t> layout_order(const Assembler& as) {
    std::vector<std::size_t> order;
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t index = 0; index < as.sections().size(); ++index) {
            if (as.sections()[index].zero_fill == (pass == 1)) order.push_back(index);
        }
    }
    return order;
}

} // namespace

std::vector<std::uint64_t> macho_section_addresses(const Assembler& as) {
    const auto& sections = as.sections();
    std::vector<std::uint64_t> addresses(sections.size());
    const auto order = layout_order(as);
    std::uint64_t address = 0;
    for (std::size_t position = 0; position < order.size(); ++position) {
        const auto& section = sections[order[position]];
        address = align_to(address, section.alignment);
        addresses[order[position]] = address;
        address += section.size;
        // Pad to the next section's alignment, as Apple's assembler does.
        if (position + 1 < order.size() && !sections[order[position + 1]].zero_fill) {
            address = align_to(address, sections[order[position + 1]].alignment);
        }
    }
    return addresses;
}

std::vector<std::uint8_t> write_macho(Assembler& as,
                                      const std::vector<std::vector<Relocation>>& relocations,
                                      bool subsections_via_symbols) {
    const auto& sections = as.sections();
    const auto& symbols = as.symbols();
    if (sections.size() > 255) {
        as.error({}, "a Mach-O object has at most 255 sections");
        return {};
    }
    const auto order = layout_order(as);

    // Symbols the linker sees: non-temporary symbols, and temporary ones a
    // relocation names.
    struct Entry {
        std::uint32_t symbol{};
        std::uint8_t section{};
    };
    std::vector<Entry> locals;
    std::vector<Entry> externals;
    std::vector<Entry> undefined;
    MergedStringTable strings;
    for (std::uint32_t index = 0; index < symbols.size(); ++index) {
        const auto& symbol = symbols[index];
        if (symbol.type == elf::stt_section) continue;
        if (symbol.temporary && !symbol.used_in_relocation) continue;
        const bool registered = symbol.defined() || symbol.binding != Symbol::Binding::Unset ||
                                symbol.used_in_relocation || symbol.private_extern ||
                                symbol.weak_reference || symbol.weak_definition ||
                                symbol.no_dead_strip;
        if (!registered) continue;
        const auto* base = as.base_symbol(symbol);
        const bool external = symbol.binding == Symbol::Binding::Global ||
                              symbol.binding == Symbol::Binding::Weak || symbol.private_extern;
        const bool defined = symbol.section.has_value() || symbol.variable.has_value();
        Entry entry{index, 0};
        if (base && base->section) entry.section = static_cast<std::uint8_t>(base->section->value + 1);
        strings.add(symbol.name);
        if (!defined) {
            undefined.push_back(entry);
        } else if (external) {
            externals.push_back(entry);
        } else {
            locals.push_back(entry);
        }
    }
    const auto by_name = [&](const Entry& a, const Entry& b) {
        return symbols[a.symbol].name < symbols[b.symbol].name;
    };
    std::sort(externals.begin(), externals.end(), by_name);
    std::sort(undefined.begin(), undefined.end(), by_name);
    std::vector<std::uint32_t> symbol_index(symbols.size(), 0);
    {
        std::uint32_t next = 0;
        for (const auto* list : {&locals, &externals, &undefined}) {
            for (const auto& entry : *list) symbol_index[entry.symbol] = next++;
        }
    }
    strings.finalize(1, 8);
    const auto symbol_count =
        static_cast<std::uint32_t>(locals.size() + externals.size() + undefined.size());

    // Sizes and offsets.
    std::uint32_t command_count = 1;
    std::uint32_t command_size = 72 + 80 * static_cast<std::uint32_t>(sections.size());
    if (symbol_count != 0) {
        command_count += 2;
        command_size += 24 + 80;
    }
    const std::uint64_t data_start = 32 + command_size;
    std::uint64_t data_size = 0;
    std::uint64_t vm_size = 0;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto address = as.section_address(SectionId{static_cast<std::uint32_t>(index)});
        vm_size = std::max(vm_size, address + sections[index].size);
        if (!sections[index].zero_fill) data_size = std::max(data_size, address + sections[index].size);
    }
    // File contents run to the last non-zero-fill section's padding.
    std::uint64_t file_size = data_size;
    file_size = align_to(file_size, 8);
    std::uint64_t relocation_offset = data_start + file_size;
    std::uint64_t relocation_count = 0;
    for (const auto& list : relocations) relocation_count += list.size();
    const std::uint64_t symbol_offset = relocation_offset + 8 * relocation_count;
    const std::uint64_t string_offset = symbol_offset + 16ULL * symbol_count;

    ByteWriter out(false);
    out.u32(mh_magic_64);
    out.u32(as.object().cpu_type);
    out.u32(as.object().cpu_subtype);
    out.u32(mh_object);
    out.u32(command_count);
    out.u32(command_size);
    out.u32(subsections_via_symbols ? mh_subsections_via_symbols : 0);
    out.u32(0);

    const auto padded = [&](const std::string& text) {
        std::array<std::uint8_t, 16> field{};
        std::memcpy(field.data(), text.data(), std::min<std::size_t>(16, text.size()));
        out.bytes(field);
    };
    out.u32(lc_segment_64);
    out.u32(72 + 80 * sections.size());
    padded("");
    out.u64(0);
    out.u64(vm_size);
    out.u64(data_start);
    out.u64(data_size);
    out.u32(7);
    out.u32(7);
    out.u32(sections.size());
    out.u32(0);
    std::uint64_t next_relocation = relocation_offset;
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        const auto address = as.section_address(SectionId{static_cast<std::uint32_t>(index)});
        padded(section.name);
        padded(section.segment);
        out.u64(address);
        out.u64(section.size);
        out.u32(section.zero_fill ? 0 : data_start + address);
        out.u32(static_cast<std::uint32_t>(std::countr_zero(section.alignment)));
        const auto count = relocations[index].size();
        out.u32(count != 0 ? next_relocation : 0);
        out.u32(count);
        out.u32(section.characteristics |
                (section.has_instructions ? macho::s_attr_some_instructions : 0));
        out.u32(0);
        out.u32(0);
        out.u32(0);
        next_relocation += 8 * count;
    }
    if (symbol_count != 0) {
        out.u32(lc_symtab);
        out.u32(24);
        out.u32(symbol_offset);
        out.u32(symbol_count);
        out.u32(string_offset);
        out.u32(strings.size());
        out.u32(lc_dysymtab);
        out.u32(80);
        out.u32(0);
        out.u32(locals.size());
        out.u32(locals.size());
        out.u32(externals.size());
        out.u32(locals.size() + externals.size());
        out.u32(undefined.size());
        for (int field = 0; field < 12; ++field) out.u32(0);
    }

    // Section contents at data_start + address, in layout order.
    for (const auto index : order) {
        const auto& section = sections[index];
        if (section.zero_fill) continue;
        const auto address = as.section_address(SectionId{static_cast<std::uint32_t>(index)});
        auto& data = out.data();
        data.resize(data_start + address, 0);
        const auto contents = as.section_contents(SectionId{static_cast<std::uint32_t>(index)});
        out.bytes(contents);
    }
    out.data().resize(data_start + file_size, 0);

    for (std::size_t index = 0; index < sections.size(); ++index) {
        // In reverse order of recording, as Apple's assembler writes them.
        for (auto relocation = relocations[index].rbegin(); relocation != relocations[index].rend();
             ++relocation) {
            const auto& target = symbols[relocation->symbol.value];
            const std::uint32_t number = relocation->external ? symbol_index[relocation->symbol.value]
                                         : target.section ? target.section->value + 1
                                                          : 0;
            out.u32(relocation->offset);
            out.u32(number | (relocation->pc_relative ? 1U << 24U : 0U) |
                    (static_cast<std::uint32_t>(relocation->length) << 25U) |
                    (relocation->external ? 1U << 27U : 0U) | (relocation->type << 28U));
        }
    }

    for (const auto* list : {&locals, &externals, &undefined}) {
        for (const auto& entry : *list) {
            const auto& symbol = symbols[entry.symbol];
            const auto* base = as.base_symbol(symbol);
            std::uint8_t type = n_undf;
            std::uint64_t value = 0;
            if (base && base->section) {
                type = n_sect;
                value = as.section_address(*base->section) + as.address(*base);
                if (base != &symbol) {
                    const auto offset = as.evaluate(*symbol.variable, false);
                    if (offset) value += static_cast<std::uint64_t>(offset->constant);
                }
            } else if (symbol.variable) {
                type = n_abs;
                if (const auto absolute = as.evaluate(*symbol.variable, true); absolute && absolute->absolute()) {
                    value = static_cast<std::uint64_t>(absolute->constant);
                }
            } else if (symbol.common) {
                value = symbol.common_size;
            }
            if (symbol.private_extern) type |= n_pext;
            if (symbol.binding == Symbol::Binding::Global || symbol.binding == Symbol::Binding::Weak ||
                symbol.private_extern || type == n_undf) {
                type |= n_ext;
            }
            std::uint16_t description = 0;
            if (symbol.no_dead_strip) description |= n_no_dead_strip;
            if (symbol.weak_reference) description |= n_weak_ref;
            if (symbol.weak_definition) description |= n_weak_def;
            if (symbol.alt_entry) description |= n_alt_entry;
            if (symbol.common && symbol.common_alignment > 1) {
                description = static_cast<std::uint16_t>(
                    (description & 0xf0ffU) |
                    (static_cast<unsigned>(std::countr_zero(symbol.common_alignment)) << 8U));
            }
            out.u32(strings.offset(symbol.name));
            out.u8(type);
            out.u8((type & 0x0eU) == n_sect ? entry.section : 0);
            out.u16(description);
            out.u64(value);
        }
    }
    if (symbol_count != 0) {
        out.u8(0);
        const auto string_bytes = strings.data(1);
        out.bytes(string_bytes);
    }
    return std::move(out.data());
}

} // namespace cross::assembler
