// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/elf_writer.hpp"
#include "linker/link.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

namespace cross::linker {
namespace {

constexpr std::uint16_t shn_abs = 0xfff1;

class SectionContext final : public RelocationContext {
public:
    SectionContext(const Link& link, const InputSection& section, std::optional<std::uint64_t> gp,
                   Diagnostics& diagnostics)
        : link_(link), section_(section), gp_(gp), diagnostics_(diagnostics) {}

    [[nodiscard]] SymbolValue symbol(std::uint32_t index) const override {
        return link_.symbol_value(*section_.file, index);
    }
    [[nodiscard]] std::uint64_t place(std::uint64_t offset) const override {
        return link_.mask(link_.address(section_) + offset);
    }
    [[nodiscard]] std::optional<std::uint64_t> gp() const override { return gp_; }
    [[nodiscard]] std::uint64_t merged(const InputSection& section,
                                       std::uint64_t offset) const override {
        return link_.section_address(section, offset);
    }
    [[nodiscard]] const InputSection& section() const override { return section_; }
    void error(std::uint64_t offset, std::string_view message) const override {
        diagnostics_.command_error(section_.location(offset) + ": " + std::string(message));
    }
    void warning(std::uint64_t offset, std::string_view message) const override {
        diagnostics_.warning({}, section_.location(offset) + ": " + std::string(message));
    }

private:
    const Link& link_;
    const InputSection& section_;
    std::optional<std::uint64_t> gp_;
    Diagnostics& diagnostics_;
};

// The name GNU ld gives the FILE symbol of an input without one.
std::string file_basename(const InputFile& file) {
    const auto& name = file.member.empty() ? file.path : file.member;
    const auto slash = name.find_last_of('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

std::string padded_hex(std::uint64_t value, int digits) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%0*llx", digits, static_cast<unsigned long long>(value));
    return buffer;
}

std::string size_field(std::uint64_t value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%#10llx", static_cast<unsigned long long>(value));
    return value == 0 ? std::string("       0x0") : std::string(buffer);
}

} // namespace

bool Link::contributes(const InputFile& file) const {
    // GNU ld writes the local symbols of a file that has a section in an
    // output section with contents, including one a target merged.
    return std::any_of(file.sections.begin(), file.sections.end(), [](const auto& section) {
        const auto* placed = section && section->combined ? section->combined : section.get();
        return placed && placed->included() && placed->output && placed->output->index != 0 &&
               !placed->output->nobits();
    });
}

bool Link::write_contents() {
    const auto pointer = gp();
    for (const auto& step : steps_) {
        auto* placement = step.placement;
        if (!placement || !placement->output || !placement->emitted) continue;
        auto& output = *placement->output;
        if (output.nobits()) continue;
        output.data.assign(output.size, 0);
        // Each gap repeats its fill pattern from the gap's start.
        final_ = true;
        for (const auto& gap : placement->padding) {
            if (!gap.fill) continue;
            const auto pattern = fill_pattern(*gap.fill);
            if (!pattern || pattern->empty()) continue;
            for (std::uint64_t index = 0; index < gap.size; ++index) {
                output.data[gap.offset + index] = (*pattern)[index % pattern->size()];
            }
        }
        for (const auto& item : placement->items) {
            for (const auto* section : item.sections) {
                if (section->nobits() || !section->included()) continue;
                std::copy(section->data.begin(), section->data.end(),
                          output.data.begin() + static_cast<std::ptrdiff_t>(section->offset));
            }
            if (item.item) {
                if (const auto* data = std::get_if<script::Data>(item.item)) {
                    for (unsigned index = 0; index < data->size; ++index) {
                        const unsigned shift = object_.big_endian ? (data->size - 1 - index) * 8 : index * 8;
                        output.data[item.offset + index] = static_cast<std::uint8_t>(item.value >> shift);
                    }
                }
            }
        }
        if (output.name == ".eh_frame") patch_frame_pointers(output);
        for (const auto* section : output.sections) {
            if (section->relocations.empty() || section->nobits()) continue;
            const SectionContext context(*this, *section, pointer, diagnostics_);
            target_->relocate(context, std::span<std::uint8_t>(output.data).subspan(section->offset,
                                                                                  section->size));
        }
    }
    // The .eh_frame_hdr table reads the relocated FDEs.
    if (auto* frames = output_named(".eh_frame"); frames && frame_header_) write_frame_header(*frames);
    return diagnostics_.errors() == 0;
}

std::uint64_t Link::entry_address() {
    const bool named = options_.entry || script_.entry;
    const auto name = options_.entry.value_or(script_.entry.value_or("_start"));
    if (const auto* target = find(name); target && target->defined()) {
        if (target->section) return section_address(*target->section, target->value);
        return target->value;
    }
    if (named && !name.empty()) {
        char* end = nullptr;
        const auto value = std::strtoull(name.c_str(), &end, 0);
        if (end && *end == '\0') return mask(value);
    }
    std::uint64_t fallback = 0;
    if (const auto* text = output_named(".text")) fallback = text->address;
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%llx", static_cast<unsigned long long>(fallback));
    diagnostics_.warning({}, "cannot find entry symbol " + name + "; defaulting to 0x" + buffer);
    return fallback;
}

bool Link::write_executable() {
    using assembler::ElfSection;
    assembler::StringTable names;
    std::vector<ElfSection> sections;
    std::vector<OutputSection*> emitted;
    for (const auto& step : steps_) {
        if (step.placement && step.placement->output && step.placement->emitted) {
            emitted.push_back(step.placement->output);
        }
    }
    for (auto* output : emitted) {
        output->index = static_cast<std::uint32_t>(sections.size() + 1);
        ElfSection section;
        section.name = names.add(output->name);
        section.type = output->type;
        section.flags = output->flags;
        section.alignment = output->alignment;
        section.entry_size = output->entry_size;
        target_->finish_output_section(output->name, options_.format, section.type, section.flags,
                                       section.entry_size);
        section.address = output->address;
        section.offset = output->offset;
        if (output->nobits()) {
            section.size = output->size;
        } else {
            section.data = std::move(output->data);
        }
        sections.push_back(std::move(section));
    }

    const bool wide = object_.elf64;
    assembler::ByteWriter symtab(object_.big_endian);
    assembler::StringTable strings;
    const auto add_symbol = [&](std::string_view name, std::uint64_t value, std::uint64_t size,
                                std::uint8_t info, std::uint8_t other, std::uint16_t shndx) {
        const auto offset = strings.add(name);
        if (wide) {
            symtab.u32(offset);
            symtab.u8(info);
            symtab.u8(other);
            symtab.u16(shndx);
            symtab.u64(value);
            symtab.u64(size);
        } else {
            symtab.u32(offset);
            symtab.u32(value);
            symtab.u32(size);
            symtab.u8(info);
            symtab.u8(other);
            symtab.u16(shndx);
        }
    };
    const auto info = [](std::uint8_t binding, std::uint8_t type) {
        return static_cast<std::uint8_t>((static_cast<unsigned>(binding) << 4U) | (type & 0xfU));
    };
    std::uint32_t count = 0;
    std::uint32_t first_global = 0;
    if (!options_.strip_all) {
        add_symbol("", 0, 0, 0, 0, 0);
        ++count;
        // GNU ld's order: a symbol for each output section, each file's
        // local symbols after a FILE symbol (its name when the file has
        // none), then the symbols the script makes local after an unnamed
        // FILE symbol, then the global symbols.
        for (const auto* output : emitted) {
            add_symbol("", output->address, 0, info(elf::stb_local, elf::stt_section), 0,
                       static_cast<std::uint16_t>(output->index));
            ++count;
        }
        bool file_symbols = false;
        for (const auto& file : files_) {
            if (file->just_symbols || !contributes(*file)) continue;
            bool named = false;
            for (std::size_t index = 1; index < file->symbols.size(); ++index) {
                const auto& entry = file->symbols[index];
                if (entry.binding != elf::stb_local || entry.type == elf::stt_section ||
                    entry.name.empty()) {
                    continue;
                }
                std::uint16_t shndx = shn_abs;
                std::uint64_t value = entry.value;
                if (entry.type != elf::stt_file && entry.shndx != shn_abs) {
                    if (entry.shndx == 0 || entry.shndx >= file->sections.size()) continue;
                    // A symbol of a section that .eh_frame editing excluded
                    // stays at the section's place (GNU ld).
                    const auto* section = file->sections[entry.shndx].get();
                    if (!section || !section->output || section->output->index == 0) continue;
                    // GNU ld drops assembler-local labels in mergeable data.
                    if ((section->flags & elf::shf_merge) != 0 &&
                        (entry.name.starts_with(".L") || entry.name.starts_with("..") ||
                         entry.name.starts_with("_.L_") || target_->local_label(entry.name))) {
                        continue;
                    }
                    shndx = static_cast<std::uint16_t>(section->output->index);
                    value = section_address(*section, entry.value);
                }
                if (entry.type == elf::stt_file) {
                    named = true;
                } else if (!named) {
                    add_symbol(file_basename(*file), 0, 0, info(elf::stb_local, elf::stt_file), 0, shn_abs);
                    ++count;
                    named = true;
                }
                file_symbols = true;
                add_symbol(entry.name, value, entry.size, info(elf::stb_local, entry.type),
                           entry.other, shndx);
                ++count;
            }
        }
        bool script_locals = false;
        for (const int pass : {0, 1}) {
            if (pass == 1) first_global = count;
            for (const auto& entry : symbols_) {
                // Undefined symbols that an object refers to stay, except
                // weak ones, which GNU ld drops from executables.
                if (!entry.defined() && (!entry.referenced || !entry.file || entry.weak ||
                                         (options_.gc_sections && !live_references_.contains(&entry)))) {
                    continue;
                }
                // Script HIDDEN symbols are local; object symbols keep their
                // binding and visibility, as in GNU ld.
                const bool local = entry.hidden;
                if (local != (pass == 0)) continue;
                std::uint16_t shndx = 0;
                std::uint64_t value = 0;
                if (entry.defined()) {
                    // A symbol of an output section that was left out moves
                    // to a nearby one (GNU ld).
                    const auto home = [&](const OutputSection* output, std::uint64_t address) {
                        if (output && output->index == 0) output = nearby_section(*output, address);
                        return output && output->index != 0 ? static_cast<std::uint16_t>(output->index) : shn_abs;
                    };
                    if (entry.section) {
                        if (!entry.section->included() || !entry.section->output) continue;
                        value = section_address(*entry.section, entry.value);
                        shndx = home(entry.section->output, value);
                    } else {
                        value = entry.value;
                        shndx = entry.output ? home(entry.output, value) : shn_abs;
                    }
                }
                if (local && !script_locals && file_symbols) {
                    add_symbol("", 0, 0, info(elf::stb_local, elf::stt_file), 0, shn_abs);
                    ++count;
                }
                script_locals = script_locals || local;
                const auto binding = local ? elf::stb_local
                                           : (entry.weak ? elf::stb_weak : elf::stb_global);
                // A symbol made local loses its visibility (GNU ld).
                const auto other = local ? static_cast<std::uint8_t>(entry.other & ~3U) : entry.other;
                add_symbol(entry.name, value, entry.size, info(binding, entry.type), other, shndx);
                ++count;
            }
        }
    }
    if (!options_.strip_all) {
        ElfSection table;
        table.name = names.add(".symtab");
        table.type = elf::sht_symtab;
        table.alignment = wide ? 8 : 4;
        table.entry_size = wide ? 24 : 16;
        table.link = static_cast<std::uint32_t>(sections.size() + 2);
        table.info = target_->sections_end_locals(options_.format)
                         ? static_cast<std::uint32_t>(emitted.size() + 1)
                         : first_global;
        table.data = std::move(symtab.data());
        sections.push_back(std::move(table));
        ElfSection string_table;
        string_table.name = names.add(".strtab");
        string_table.type = elf::sht_strtab;
        string_table.data = strings.data();
        sections.push_back(std::move(string_table));
    }
    ElfSection section_names;
    section_names.name = names.add(".shstrtab");
    section_names.type = elf::sht_strtab;
    sections.push_back(std::move(section_names));
    sections.back().data = names.data();

    assembler::ElfExecutable executable;
    executable.entry = entry_address();
    for (const auto& segment : segments_) {
        assembler::ElfSegment header;
        header.type = segment.type;
        header.flags = segment.flags;
        header.offset = segment.offset;
        header.address = segment.address;
        header.physical_address = segment.physical_address;
        header.file_size = segment.file_size;
        header.memory_size = segment.memory_size;
        header.alignment = segment.alignment;
        executable.segments.push_back(header);
    }
    if (diagnostics_.errors() != 0) return false;
    if (options_.fatal_warnings && diagnostics_.warnings() != 0) return false;
    const auto bytes = assembler::write_elf(object_, target_->output_flags(), sections,
                                            static_cast<std::uint32_t>(sections.size()), &executable);
    std::ofstream file(options_.output, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) {
        diagnostics_.command_error("cannot write '" + options_.output.string() + "'");
        return false;
    }
    std::error_code code;
    std::filesystem::permissions(options_.output,
                                 std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
                                     std::filesystem::perms::others_exec,
                                 std::filesystem::perm_options::add, code);
    return true;
}

void Link::write_map() const {
    std::ostringstream out;
    // Addresses take the target's width, as in GNU ld's map.
    const int width = object_.elf64 ? 16 : 8;
    out << "Discarded input sections\n\n";
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || section->included() || section->size == 0) continue;
            out << ' ' << section->name;
            if (section->name.size() >= 15) {
                out << "\n" << std::string(16, ' ');
            } else {
                out << std::string(15 - section->name.size(), ' ');
            }
            out << padded_hex(0, width) << ' ' << size_field(section->size) << ' ' << file->name()
                << '\n';
        }
    }
    out << "\nLinker script and memory map\n\n";
    std::vector<const Symbol*> defined;
    for (const auto& entry : symbols_) {
        if (entry.defined() && entry.section && entry.section->included()) defined.push_back(&entry);
    }
    std::stable_sort(defined.begin(), defined.end(), [&](const Symbol* a, const Symbol* b) {
        return section_address(*a->section, a->value) < section_address(*b->section, b->value);
    });
    // An assignment line: the value and the symbol (or `.`); inactive
    // PROVIDE assignments define nothing.
    const auto assignment_line = [&](const script::Assignment& assignment, std::uint64_t value) {
        if (assignment.symbol != ".") {
            const auto* target = find(assignment.symbol);
            if (!target || !target->script) return;
        }
        out << std::string(16, ' ') << padded_hex(value, width) << std::string(16, ' ')
            << assignment.symbol << '\n';
    };
    for (const auto& step : steps_) {
        if (!step.placement) {
            if (const auto* assignment = std::get_if<script::Assignment>(step.command)) {
                assignment_line(*assignment, step.value);
            }
            continue;
        }
        // Sections left out of the output are listed too (GNU ld).
        const auto* output = step.placement->output;
        if (!output || (!step.placement->emitted && !step.placement->created)) continue;
        out << '\n' << output->name;
        if (output->name.size() >= 16) {
            out << '\n' << std::string(16, ' ');
        } else {
            out << std::string(16 - output->name.size(), ' ');
        }
        out << padded_hex(output->address, width) << ' ' << size_field(output->size);
        if (output->load_address != output->address) {
            out << " load address " << padded_hex(output->load_address, width);
        }
        out << '\n';
        for (const auto& item : step.placement->items) {
            if (item.item) {
                if (const auto* assignment = std::get_if<script::Assignment>(item.item)) {
                    assignment_line(*assignment, item.value);
                    continue;
                }
                if (const auto* data = std::get_if<script::Data>(item.item)) {
                    static constexpr const char* names[] = {"", "BYTE", "SHORT", "", "LONG",
                                                            "", "", "", "QUAD"};
                    out << ' ' << std::string(15, ' ') << padded_hex(output->address + item.offset, width)
                        << ' ' << size_field(data->size) << ' ' << names[data->size] << ' '
                        << padded_hex(item.value, 1) << '\n';
                    continue;
                }
            }
            for (const auto* section : item.sections) {
                out << ' ' << section->name;
                if (section->name.size() >= 15) {
                    out << '\n' << std::string(16, ' ');
                } else {
                    out << std::string(15 - section->name.size(), ' ');
                }
                out << padded_hex(address(*section), width) << ' ' << size_field(section->size) << ' '
                    << section->file->name() << '\n';
                for (const auto* entry : defined) {
                    if (entry->section != section) continue;
                    out << std::string(16, ' ') << padded_hex(section_address(*section, entry->value), width)
                        << std::string(16, ' ') << entry->name << '\n';
                }
            }
        }
    }
    const auto text = out.str();
    if (*options_.map == "-") {
        std::cout << text;
        return;
    }
    std::ofstream file(*options_.map, std::ios::binary | std::ios::trunc);
    file << text;
    if (!file) diagnostics_.command_error("cannot write '" + options_.map->string() + "'");
}

} // namespace cross::linker
