// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/coff_writer.hpp"

#include "assembler/elf_writer.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <map>

namespace cross::assembler {
namespace {

constexpr std::int16_t section_absolute = -1;
constexpr std::int16_t section_debug = -2;
constexpr std::uint32_t weak_search_alias = 3;
constexpr std::uint32_t scn_lnk_nreloc_ovfl = 0x01000000;

// CRC-32 (reflected, polynomial 0xedb88320) from 0 without a final
// complement, the section checksum LLVM MC and link.exe use.
std::uint32_t section_checksum(const std::vector<std::uint8_t>& bytes) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> entries{};
        for (std::uint32_t index = 0; index < 256; ++index) {
            std::uint32_t value = index;
            for (int bit = 0; bit < 8; ++bit) value = (value >> 1U) ^ ((value & 1U) != 0 ? 0xedb88320U : 0U);
            entries[index] = value;
        }
        return entries;
    }();
    std::uint32_t crc = 0;
    for (const auto byte : bytes) crc = table[(crc ^ byte) & 0xffU] ^ (crc >> 8U);
    return crc;
}

struct CoffSymbol {
    std::string name;
    std::uint32_t value{};
    std::int16_t section{};
    std::uint16_t type{};
    std::uint8_t storage_class{};
    // Auxiliary records: raw 18-byte entries.
    std::vector<std::array<std::uint8_t, 18>> aux;
    // A section definition's section, or a weak external's default symbol.
    std::optional<std::size_t> section_definition;
    std::optional<std::size_t> weak_default;
    std::uint32_t index{};
};

} // namespace

std::vector<std::uint8_t> write_coff(Assembler& as,
                                     const std::vector<std::vector<Relocation>>& relocations,
                                     const std::vector<std::string>& file_names) {
    const auto& sections = as.sections();
    const auto& symbols = as.symbols();

    // Section numbers: associative COMDAT sections after the others, so no
    // association points forward.
    std::vector<std::int16_t> number(sections.size());
    {
        std::int16_t next = 1;
        for (int pass = 0; pass < 2; ++pass) {
            for (std::size_t index = 0; index < sections.size(); ++index) {
                const bool associative = sections[index].selection == coff::comdat_select_associative;
                if (associative == (pass == 1)) number[index] = next++;
            }
        }
        if (sections.size() > 32767) {
            as.error({}, "the object would have more than 32767 sections");
            return {};
        }
    }

    std::vector<CoffSymbol> table;
    std::map<std::uint32_t, std::size_t> by_symbol;
    std::vector<std::size_t> section_entry(sections.size());
    const auto symbol_section = [&](const Symbol& symbol) -> std::int16_t {
        const auto* base = as.base_symbol(symbol);
        if (base && base->section) return number[base->section->value];
        if (symbol.variable) return section_absolute;
        return 0;
    };
    const auto symbol_value = [&](const Symbol& symbol) -> std::uint32_t {
        if (symbol.common) return static_cast<std::uint32_t>(symbol.common_size);
        const auto* base = as.base_symbol(symbol);
        if (base && base->section) {
            if (base == &symbol) return static_cast<std::uint32_t>(as.address(symbol));
            const auto value = as.evaluate(*symbol.variable, false);
            return static_cast<std::uint32_t>(as.address(*base) +
                                              static_cast<std::uint64_t>(value ? value->constant : 0));
        }
        if (symbol.variable) {
            if (const auto value = as.evaluate(*symbol.variable, true); value && value->absolute()) {
                return static_cast<std::uint32_t>(value->constant);
            }
        }
        return 0;
    };
    const auto fill = [&](CoffSymbol& entry, const Symbol& symbol) {
        entry.section = symbol_section(symbol);
        entry.value = symbol_value(symbol);
        entry.type = symbol.coff_type;
        entry.storage_class = symbol.storage_class;
        if (entry.storage_class == 0) {
            const bool external = symbol.binding == Symbol::Binding::Global ||
                                  symbol.binding == Symbol::Binding::Weak ||
                                  (!symbol.section && !symbol.variable);
            entry.storage_class = external ? coff::sym_class_external : coff::sym_class_static;
        }
    };

    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        CoffSymbol entry;
        entry.name = section.name;
        entry.section = number[index];
        entry.storage_class = coff::sym_class_static;
        entry.section_definition = index;
        entry.aux.emplace_back();
        section_entry[index] = table.size();
        table.push_back(std::move(entry));
        if (section.group && section.selection != coff::comdat_select_associative &&
            !by_symbol.contains(section.group->value)) {
            CoffSymbol comdat;
            const auto& symbol = symbols[section.group->value];
            comdat.name = symbol.name;
            fill(comdat, symbol);
            comdat.section = number[index];
            by_symbol[section.group->value] = table.size();
            table.push_back(std::move(comdat));
        }
    }
    std::vector<std::size_t> weak_defaults;
    for (std::uint32_t index = 0; index < symbols.size(); ++index) {
        const auto& symbol = symbols[index];
        if (symbol.type == elf::stt_section || by_symbol.contains(index)) continue;
        if (symbol.temporary && symbol.storage_class != coff::sym_class_static) continue;
        const bool registered = symbol.defined() || symbol.binding != Symbol::Binding::Unset ||
                                symbol.used_in_relocation || symbol.storage_class != 0;
        if (!registered) continue;
        if (symbol.binding == Symbol::Binding::Local && !symbol.defined()) {
            as.error(symbol.location, "local symbol '" + symbol.name + "' is not defined");
            continue;
        }
        CoffSymbol entry;
        entry.name = symbol.name;
        if (symbol.binding == Symbol::Binding::Weak) {
            // A weak external names a default definition: the symbol's own
            // definition, or absolute zero.
            entry.storage_class = coff::sym_class_weak_external;
            CoffSymbol fallback;
            fallback.name = ".weak." + symbol.name + ".default";
            fill(fallback, symbol);
            if (fallback.section == 0) fallback.section = section_absolute;
            entry.aux.emplace_back();
            by_symbol[index] = table.size();
            entry.weak_default = table.size() + 1;
            table.push_back(std::move(entry));
            weak_defaults.push_back(table.size());
            table.push_back(std::move(fallback));
            continue;
        }
        fill(entry, symbol);
        by_symbol[index] = table.size();
        table.push_back(std::move(entry));
    }
    if (!weak_defaults.empty()) {
        // Default names stay unique across objects through the first
        // defined external symbol, preferring one outside a COMDAT.
        const CoffSymbol* unique = nullptr;
        for (const bool allow_comdat : {false, true}) {
            for (std::size_t index = 0; index < table.size() && !unique; ++index) {
                const auto& entry = table[index];
                if (std::find(weak_defaults.begin(), weak_defaults.end(), index) != weak_defaults.end() ||
                    entry.storage_class != coff::sym_class_external || entry.section == 0) {
                    continue;
                }
                if (!allow_comdat && entry.section > 0) {
                    const auto section_index = static_cast<std::size_t>(
                        std::find(number.begin(), number.end(), entry.section) - number.begin());
                    if ((sections[section_index].characteristics & coff::scn_lnk_comdat) != 0) continue;
                }
                unique = &entry;
            }
            if (unique) break;
        }
        if (unique) {
            const auto suffix = "." + unique->name;
            for (const auto index : weak_defaults) table[index].name += suffix;
        }
    }
    for (const auto& name : file_names) {
        CoffSymbol entry;
        entry.name = ".file";
        entry.section = section_debug;
        entry.storage_class = coff::sym_class_file;
        for (std::size_t offset = 0; offset < name.size(); offset += 18) {
            std::array<std::uint8_t, 18> record{};
            const auto count = std::min<std::size_t>(18, name.size() - offset);
            std::memcpy(record.data(), name.data() + offset, count);
            entry.aux.push_back(record);
        }
        table.push_back(std::move(entry));
    }
    std::uint32_t symbol_count = 0;
    for (auto& entry : table) {
        entry.index = symbol_count;
        symbol_count += 1 + static_cast<std::uint32_t>(entry.aux.size());
    }

    // The string table holds section and symbol names longer than eight bytes.
    MergedStringTable strings;
    for (const auto& section : sections) {
        if (section.name.size() > 8) strings.add(section.name);
    }
    for (const auto& entry : table) {
        if (entry.name.size() > 8) strings.add(entry.name);
    }
    strings.finalize(4);

    // File layout: header, section headers, then each section's data and
    // relocations in creation order, then the symbol and string tables.
    struct Layout {
        std::vector<std::uint8_t> data;
        std::uint32_t raw_pointer{};
        std::uint32_t relocation_pointer{};
        std::uint32_t checksum{};
    };
    std::vector<Layout> layout(sections.size());
    std::uint32_t offset = 20 + 40 * static_cast<std::uint32_t>(sections.size());
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        auto& place = layout[index];
        if (!section.zero_fill) {
            place.data = as.section_contents(SectionId{static_cast<std::uint32_t>(index)});
            place.raw_pointer = offset;
            offset += static_cast<std::uint32_t>(place.data.size());
        }
        place.checksum = section_checksum(place.data);
        if (!relocations[index].empty()) {
            place.relocation_pointer = offset;
            if (relocations[index].size() >= 0xffff) offset += 10;
            offset += 10 * static_cast<std::uint32_t>(relocations[index].size());
        }
    }
    const std::uint32_t symbol_table = offset;

    ByteWriter out(false);
    out.u16(as.object().machine);
    out.u16(sections.size());
    out.u32(0);
    out.u32(symbol_table);
    out.u32(symbol_count);
    out.u16(0);
    out.u16(0);

    const auto write_name = [&](const std::string& name) {
        std::array<std::uint8_t, 8> field{};
        if (name.size() <= 8) {
            std::memcpy(field.data(), name.data(), name.size());
        } else {
            const auto text = "/" + std::to_string(strings.offset(name));
            std::memcpy(field.data(), text.data(), std::min<std::size_t>(8, text.size()));
        }
        out.bytes(field);
    };
    std::vector<std::size_t> header_order(sections.size());
    for (std::size_t index = 0; index < sections.size(); ++index) {
        header_order[static_cast<std::size_t>(number[index] - 1)] = index;
    }
    for (const auto index : header_order) {
        const auto& section = sections[index];
        const auto& place = layout[index];
        if (section.alignment > 8192) {
            as.error({}, "section '" + section.name + "' is aligned beyond 8192 bytes, which COFF cannot express");
            return {};
        }
        std::uint32_t characteristics = section.characteristics;
        characteristics |= static_cast<std::uint32_t>(std::countr_zero(section.alignment) + 1) << 20U;
        const auto count = relocations[index].size();
        if (count >= 0xffff) characteristics |= scn_lnk_nreloc_ovfl;
        if (strings.offset(section.name) > 9999999) {
            as.error({}, "the COFF string table is too large for section names");
            return {};
        }
        write_name(section.name);
        out.u32(0);
        out.u32(0);
        out.u32(section.size);
        out.u32(place.raw_pointer);
        out.u32(place.relocation_pointer);
        out.u32(0);
        out.u16(count >= 0xffff ? 0xffff : count);
        out.u16(0);
        out.u32(characteristics);
    }
    for (std::size_t index = 0; index < sections.size(); ++index) {
        out.bytes(layout[index].data);
        const auto& entries = relocations[index];
        if (entries.empty()) continue;
        if (entries.size() >= 0xffff) {
            out.u32(entries.size() + 1);
            out.u32(0);
            out.u16(0);
        }
        for (const auto& relocation : entries) {
            std::uint32_t symbol_index = 0;
            const auto& target = symbols[relocation.symbol.value];
            if (target.type == elf::stt_section) {
                symbol_index = table[section_entry[target.section->value]].index;
            } else if (const auto found = by_symbol.find(relocation.symbol.value); found != by_symbol.end()) {
                symbol_index = table[found->second].index;
            }
            out.u32(relocation.offset);
            out.u32(symbol_index);
            out.u16(relocation.type);
        }
    }
    for (const auto& entry : table) {
        if (entry.name.size() > 8) {
            // Long names: four zero bytes, then the string table offset.
            out.u32(0);
            out.u32(strings.offset(entry.name));
        } else {
            write_name(entry.name);
        }
        out.u32(entry.value);
        out.u16(static_cast<std::uint16_t>(entry.section));
        out.u16(entry.type);
        out.u8(entry.storage_class);
        out.u8(entry.aux.size());
        if (entry.section_definition) {
            const auto index = *entry.section_definition;
            const auto& section = sections[index];
            const auto count = relocations[index].size();
            std::uint16_t associated = static_cast<std::uint16_t>(number[index]);
            if (section.selection == coff::comdat_select_associative && section.group) {
                const auto* base = as.base_symbol(symbols[section.group->value]);
                if (!base || !base->section) {
                    as.error({}, "cannot make section '" + section.name +
                                     "' associative with sectionless symbol '" +
                                     symbols[section.group->value].name + "'");
                    return {};
                }
                associated = static_cast<std::uint16_t>(number[base->section->value]);
            }
            out.u32(section.size);
            out.u16(count >= 0xffff ? 0xffff : count);
            out.u16(0);
            out.u32(layout[index].checksum);
            out.u16(associated);
            out.u8(section.selection);
            out.u8(0);
            out.u16(0);
            continue;
        }
        if (entry.weak_default) {
            out.u32(table[*entry.weak_default].index);
            out.u32(weak_search_alias);
            for (int byte = 0; byte < 10; ++byte) out.u8(0);
            continue;
        }
        for (const auto& record : entry.aux) out.bytes(record);
    }
    out.u32(strings.size());
    const auto string_bytes = strings.data(4);
    out.bytes(string_bytes);
    return std::move(out.data());
}

} // namespace cross::assembler
