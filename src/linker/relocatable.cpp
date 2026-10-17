// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/elf_writer.hpp"
#include "linker/link.hpp"

#include <algorithm>
#include <fstream>
#include <map>

namespace cross::linker {
namespace {

constexpr std::uint32_t shn_abs = 0xfff1;
constexpr std::uint16_t em_mips = 8;
constexpr std::uint32_t grp_comdat = 1;

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

} // namespace

bool Link::write_relocatable() {
    // --defsym assignments, the only script a relocatable link takes.
    final_ = true;
    for (const auto& command : script_.commands) {
        if (const auto* assignment = std::get_if<script::Assignment>(&command)) {
            assign(*assignment, nullptr, true);
        }
    }
    if (diagnostics_.errors() != 0) return false;
    prune_records();
    // Input sections of one name share an output section, except members of
    // a section group, which keep their own so that the group survives.
    std::unordered_map<std::string, OutputSection*> by_name;
    std::vector<OutputSection*> order;
    std::vector<std::pair<const InputFile*, std::size_t>> groups;
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included()) continue;
            OutputSection* output = section->group ? nullptr : by_name[section->name];
            const bool fresh = output == nullptr;
            if (fresh) {
                auto created = std::make_unique<OutputSection>();
                created->name = section->name;
                created->type = section->type;
                created->flags = section->flags;
                created->entry_size = section->entry_size;
                output = created.get();
                outputs_.push_back(std::move(created));
                order.push_back(output);
                if (!section->group) by_name[section->name] = output;
            } else {
                constexpr auto merge = elf::shf_merge | elf::shf_strings;
                if ((output->flags & merge) != (section->flags & merge) ||
                    output->entry_size != section->entry_size) {
                    output->flags &= ~merge;
                    output->entry_size = 0;
                }
                output->flags |= section->flags & ~merge;
                if (output->type != section->type) {
                    output->type = output->nobits() && section->nobits() ? elf::sht_nobits
                                                                         : elf::sht_progbits;
                }
            }
            section->output = output;
            section->offset = align_up(output->size, section->alignment);
            output->size = section->offset + section->size;
            output->alignment = std::max(output->alignment, section->alignment);
            output->sections.push_back(section.get());
        }
        for (std::size_t index = 0; index < file->groups.size(); ++index) {
            const auto& members = file->groups[index].members;
            if (std::any_of(members.begin(), members.end(), [&](std::uint32_t member) {
                    return member < file->sections.size() && file->sections[member] &&
                           file->sections[member]->included();
                })) {
                groups.emplace_back(file.get(), index);
            }
        }
    }
    for (auto* output : order) {
        if (output->nobits()) continue;
        output->data.assign(output->size, 0);
        for (const auto* section : output->sections) {
            std::copy(section->data.begin(), section->data.end(),
                      output->data.begin() + static_cast<std::ptrdiff_t>(section->offset));
        }
    }

    // Section indices: groups first, as ELF requires, then each output
    // section followed by its relocations, then the tables.
    std::uint32_t next = static_cast<std::uint32_t>(groups.size()) + 1;
    std::unordered_map<const OutputSection*, std::uint32_t> relocation_index;
    for (auto* output : order) {
        output->index = next++;
        const bool relocated = std::any_of(output->sections.begin(), output->sections.end(),
                                           [](const InputSection* section) {
                                               return !section->relocations.empty();
                                           });
        if (relocated) relocation_index[output] = next++;
    }
    const std::uint32_t symtab_index = next;

    // Symbols: a section symbol for each output section, the files' local
    // symbols, then the globals; common symbols stay common.
    const bool wide = object_.elf64;
    assembler::ByteWriter symtab(object_.big_endian);
    assembler::StringTable strings;
    std::uint32_t count = 0;
    const auto add_symbol = [&](std::string_view name, std::uint64_t value, std::uint64_t size,
                                std::uint8_t binding, std::uint8_t type, std::uint8_t other,
                                std::uint32_t shndx) {
        const auto offset = strings.add(name);
        const auto info = static_cast<std::uint8_t>((static_cast<unsigned>(binding) << 4U) | (type & 0xfU));
        symtab.u32(offset);
        if (wide) {
            symtab.u8(info);
            symtab.u8(other);
            symtab.u16(shndx);
            symtab.u64(value);
            symtab.u64(size);
        } else {
            symtab.u32(value);
            symtab.u32(size);
            symtab.u8(info);
            symtab.u8(other);
            symtab.u16(shndx);
        }
        return count++;
    };
    add_symbol("", 0, 0, 0, 0, 0, 0);
    std::unordered_map<const OutputSection*, std::uint32_t> section_symbol;
    for (auto* output : order) {
        section_symbol[output] = add_symbol("", 0, 0, elf::stb_local, elf::stt_section, 0, output->index);
    }
    std::map<std::pair<const InputFile*, std::uint32_t>, std::uint32_t> local_index;
    for (const auto& file : files_) {
        if (file->just_symbols) continue;
        for (std::uint32_t index = 1; index < file->symbols.size(); ++index) {
            const auto& entry = file->symbols[index];
            if (entry.binding != elf::stb_local || entry.type == elf::stt_section) continue;
            std::uint32_t shndx = shn_abs;
            std::uint64_t value = entry.value;
            if (entry.type != elf::stt_file && entry.shndx != shn_abs) {
                const auto* section = entry.shndx < file->sections.size() ? file->sections[entry.shndx].get()
                                                                          : nullptr;
                if (!section || !section->included()) continue;
                shndx = section->output->index;
                value += section->offset;
            }
            local_index[{file.get(), index}] =
                add_symbol(entry.name, value, entry.size, elf::stb_local, entry.type, entry.other, shndx);
        }
    }
    const auto first_global = count;
    std::unordered_map<const Symbol*, std::uint32_t> global_index;
    for (const auto& entry : symbols_) {
        std::uint32_t shndx = 0;
        std::uint64_t value = 0;
        std::uint64_t size = entry.size;
        if (entry.kind == Symbol::Kind::Common) {
            shndx = entry.common_index;
            value = entry.common_alignment;
        } else if (entry.defined()) {
            if (entry.section) {
                if (!entry.section->included()) continue;
                shndx = entry.section->output->index;
                value = entry.section->offset + entry.value;
            } else {
                shndx = shn_abs;
                value = entry.value;
            }
        } else if (!entry.referenced || !entry.file) {
            continue;
        } else {
            size = 0;
        }
        global_index[&entry] = add_symbol(entry.name, value, size,
                                          entry.weak ? elf::stb_weak : elf::stb_global, entry.type,
                                          entry.other, shndx);
    }

    // Relocations, against the output symbols; a section symbol's addend
    // grows by its input section's offset.
    const auto entry_size = wide ? (object_.rela ? 24U : 16U) : (object_.rela ? 12U : 8U);
    std::vector<assembler::ElfSection> sections;
    assembler::StringTable names;
    std::unordered_map<const OutputSection*, std::vector<std::uint8_t>> relocations;
    for (auto* output : order) {
        if (!relocation_index.contains(output)) continue;
        assembler::ByteWriter entries(object_.big_endian);
        for (auto* section : output->sections) {
            if (section->relocations.empty()) continue;
            if (section->rela != object_.rela) {
                diagnostics_.command_error(section->location(0) + ": mixes REL and RELA relocations");
                return false;
            }
            const auto addends = target_->addends(*section);
            const auto& file = *section->file;
            for (std::size_t index = 0; index < section->relocations.size(); ++index) {
                const auto& relocation = section->relocations[index];
                std::uint32_t symbol = 0;
                std::int64_t delta = 0;
                std::uint32_t type = relocation.type;
                const auto source = relocation.symbol.value;
                bool dropped = false;
                if (source != 0 && source < file.symbols.size()) {
                    const auto& entry = file.symbols[source];
                    if (entry.global) {
                        const auto found = global_index.find(entry.global);
                        dropped = found == global_index.end();
                        if (!dropped) symbol = found->second;
                    } else if (entry.type == elf::stt_section) {
                        const auto* target = entry.shndx < file.sections.size()
                                                 ? file.sections[entry.shndx].get()
                                                 : nullptr;
                        dropped = !target || !target->included();
                        if (!dropped) {
                            symbol = section_symbol[target->output];
                            delta = static_cast<std::int64_t>(target->offset);
                        }
                    } else {
                        const auto found = local_index.find({&file, source});
                        dropped = found == local_index.end();
                        if (!dropped) symbol = found->second;
                    }
                }
                if (dropped) {
                    // Debugging data of a discarded group member refers to
                    // nothing; code may not.
                    if (section->alloc()) {
                        diagnostics_.command_error(section->location(relocation.offset) +
                                                   ": relocation refers to '" + file.symbols[source].name +
                                                   "', which is in a discarded section");
                        continue;
                    }
                    type = 0;
                }
                const auto offset = section->offset + relocation.offset;
                const auto addend = addends[index] + delta;
                if (!object_.rela && delta != 0 && type != 0) {
                    const std::span<std::uint8_t> field(output->data.data() + offset, output->data.size() - offset);
                    if (!target_->store_addend(type & 0xffU, addend, field)) {
                        diagnostics_.command_error(section->location(relocation.offset) +
                                                   ": the relocation's addend does not fit its field");
                    }
                }
                if (wide) {
                    entries.u64(offset);
                    if (object_.machine == em_mips) {
                        entries.u32(symbol);
                        entries.u8(0);
                        entries.u8(type >> 16U);
                        entries.u8(type >> 8U);
                        entries.u8(type);
                    } else {
                        entries.u64((std::uint64_t{symbol} << 32U) | type);
                    }
                    if (object_.rela) entries.u64(static_cast<std::uint64_t>(addend));
                } else {
                    entries.u32(offset);
                    entries.u32((std::uint64_t{symbol} << 8U) | (type & 0xffU));
                    if (object_.rela) entries.u32(static_cast<std::uint64_t>(addend));
                }
            }
        }
        relocations[output] = std::move(entries.data());
    }
    if (diagnostics_.errors() != 0) return false;

    for (const auto& [file, index] : groups) {
        const auto& group = file->groups[index];
        assembler::ByteWriter members(object_.big_endian);
        members.u32(group.comdat ? grp_comdat : 0);
        for (const auto member : group.members) {
            const auto* section = member < file->sections.size() ? file->sections[member].get() : nullptr;
            if (!section || !section->included()) continue;
            members.u32(section->output->index);
            if (relocation_index.contains(section->output)) members.u32(relocation_index[section->output]);
        }
        std::uint32_t signature = 0;
        const auto& entry = file->symbols[group.symbol];
        if (entry.global) {
            signature = global_index[entry.global];
        } else if (entry.type == elf::stt_section) {
            const auto* section = file->sections[entry.shndx].get();
            if (section && section->output) signature = section_symbol[section->output];
        } else {
            signature = local_index[{file, group.symbol}];
        }
        assembler::ElfSection header;
        header.name = names.add(".group");
        header.type = elf::sht_group;
        header.alignment = 4;
        header.entry_size = 4;
        header.link = symtab_index;
        header.info = signature;
        header.data = std::move(members.data());
        sections.push_back(std::move(header));
    }
    for (auto* output : order) {
        assembler::ElfSection header;
        header.name = names.add(output->name);
        header.type = output->type;
        header.flags = output->flags;
        header.alignment = output->alignment;
        header.entry_size = output->entry_size;
        if (output->nobits()) {
            header.size = output->size;
        } else {
            header.data = std::move(output->data);
        }
        sections.push_back(std::move(header));
        if (!relocation_index.contains(output)) continue;
        assembler::ElfSection table;
        table.name = names.add((object_.rela ? ".rela" : ".rel") + output->name);
        table.type = object_.rela ? elf::sht_rela : elf::sht_rel;
        table.flags = elf::shf_info_link | (output->flags & elf::shf_group);
        table.alignment = wide ? 8 : 4;
        table.entry_size = entry_size;
        table.link = symtab_index;
        table.info = output->index;
        table.data = std::move(relocations[output]);
        sections.push_back(std::move(table));
    }
    assembler::ElfSection table;
    table.name = names.add(".symtab");
    table.type = elf::sht_symtab;
    table.alignment = wide ? 8 : 4;
    table.entry_size = wide ? 24 : 16;
    table.link = symtab_index + 1;
    table.info = first_global;
    table.data = std::move(symtab.data());
    sections.push_back(std::move(table));
    assembler::ElfSection string_table;
    string_table.name = names.add(".strtab");
    string_table.type = elf::sht_strtab;
    string_table.data = strings.data();
    sections.push_back(std::move(string_table));
    assembler::ElfSection section_names;
    section_names.name = names.add(".shstrtab");
    section_names.type = elf::sht_strtab;
    sections.push_back(std::move(section_names));
    sections.back().data = names.data();

    const auto bytes = assembler::write_elf(object_, target_->output_flags(), sections,
                                            static_cast<std::uint32_t>(sections.size()));
    std::ofstream out(options_.output, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) {
        diagnostics_.command_error("cannot write '" + options_.output.string() + "'");
        return false;
    }
    return true;
}

} // namespace cross::linker
