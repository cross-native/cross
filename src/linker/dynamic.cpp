// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/link.hpp"

#include <algorithm>

namespace cross::linker {
namespace {

constexpr std::uint32_t sht_dynamic = 6;
constexpr std::uint32_t sht_dynsym = 11;

// Dynamic tags (gABI).
enum : std::uint64_t {
    dt_null = 0,
    dt_strtab = 5,
    dt_symtab = 6,
    dt_rela = 7,
    dt_relasz = 8,
    dt_relaent = 9,
    dt_strsz = 10,
    dt_syment = 11,
    dt_rel = 17,
    dt_relsz = 18,
    dt_relent = 19,
    dt_debug = 21,
    dt_relacount = 0x6ffffff9,
    dt_relcount = 0x6ffffffa,
    dt_flags_1 = 0x6ffffffb,
};
constexpr std::uint64_t df_1_pie = 0x08000000;
// The tags .dynamic holds, in ld.lld's order.
constexpr std::size_t dynamic_tags = 11;

} // namespace

void Link::create_dynamic_sections() {
    // The sections belong to the first object, GNU ld's dynamic object.
    InputFile* owner = nullptr;
    for (const auto& file : files_) {
        if (!file->raw && !file->just_symbols) {
            owner = file.get();
            break;
        }
    }
    const bool wide = object_.elf64;
    const auto make = [&](std::string name, std::uint32_t type, std::uint64_t flags, std::uint64_t entry_size,
                          std::uint64_t size) {
        auto section = std::make_unique<InputSection>();
        section->file = owner;
        section->name = std::move(name);
        section->type = type;
        section->flags = flags;
        section->alignment = entry_size == 0 ? 1 : (wide ? 8 : 4);
        section->entry_size = entry_size;
        section->size = size;
        section->data.assign(size, 0);
        section->keep = true;
        auto* result = section.get();
        owner->sections.push_back(std::move(section));
        return result;
    };
    const std::uint64_t symbol_size = wide ? 24 : 16;
    const std::uint64_t tag_size = wide ? 16 : 8;
    const auto alloc = elf::shf_alloc;
    // An empty symbol table and string table: relative relocations name no
    // symbol, but loaders such as glibc's static-pie startup expect them.
    dynamic_symbols_ = make(".dynsym", sht_dynsym, alloc, symbol_size, symbol_size);
    dynamic_strings_ = make(".dynstr", elf::sht_strtab, alloc, 0, 1);
    dynamic_relocations_ = object_.rela ? make(".rela.dyn", elf::sht_rela, alloc, wide ? 24 : 12, 0)
                                        : make(".rel.dyn", elf::sht_rel, alloc, wide ? 16 : 8, 0);
    dynamic_ = make(".dynamic", sht_dynamic, alloc | elf::shf_write, tag_size, tag_size * dynamic_tags);
}

void Link::size_dynamic_relocations(std::size_t count) {
    relative_count_ = count;
    dynamic_relocations_->size = count * dynamic_relocations_->entry_size;
    dynamic_relocations_->data.assign(dynamic_relocations_->size, 0);
}

void Link::add_relative(const InputSection& section, std::uint64_t offset, std::uint64_t value,
                        std::string_view what) {
    const auto* output = section.output;
    if (!output || (output->flags & elf::shf_write) == 0) {
        diagnostics_.command_error(section.location(offset) + ": " + std::string(what) +
                                   " needs a dynamic relocation in read-only section '" +
                                   (output ? output->name : section.name) + "'");
    }
    relatives_.emplace_back(mask(address(section) + offset), value);
}

void Link::write_dynamic_sections() {
    if (diagnostics_.errors() != 0) return;
    for (const auto* section : {dynamic_, dynamic_relocations_, dynamic_symbols_, dynamic_strings_}) {
        if (!section->included() || !section->output || section->output->nobits() ||
            section->output->data.size() < section->offset + section->size) {
            diagnostics_.command_error("a position-independent executable needs " + section->name +
                                       ", which the linker script discards");
            return;
        }
    }
    if (relatives_.size() != relative_count_) {
        diagnostics_.command_error("internal error: " + std::to_string(relatives_.size()) +
                                   " relative relocations where the layout made room for " +
                                   std::to_string(relative_count_));
        return;
    }
    const bool wide = object_.elf64;
    const bool rela = object_.rela;
    const auto put = [&](const InputSection& section, std::uint64_t offset, std::uint64_t value, unsigned size) {
        auto& data = section.output->data;
        const auto at = section.offset + offset;
        for (unsigned index = 0; index < size; ++index) {
            const unsigned shift = object_.big_endian ? (size - 1 - index) * 8 : index * 8;
            data[at + index] = static_cast<std::uint8_t>(value >> shift);
        }
    };
    // Relative relocations in address order (ld.lld), each also holding
    // its value in the field.
    std::sort(relatives_.begin(), relatives_.end());
    const auto type = target_->relative_relocation();
    const unsigned word = wide ? 8 : 4;
    std::uint64_t at = 0;
    for (const auto& [place, value] : relatives_) {
        put(*dynamic_relocations_, at, place, word);
        put(*dynamic_relocations_, at + word, type, word);
        if (rela) put(*dynamic_relocations_, at + 2 * word, value, word);
        at += dynamic_relocations_->entry_size;
    }
    const auto address_of = [&](const InputSection* section) { return section_address(*section, 0); };
    const std::pair<std::uint64_t, std::uint64_t> tags[dynamic_tags] = {
        {dt_flags_1, df_1_pie},
        {dt_debug, 0},
        {rela ? dt_rela : dt_rel, address_of(dynamic_relocations_)},
        {rela ? dt_relasz : dt_relsz, dynamic_relocations_->size},
        {rela ? dt_relaent : dt_relent, dynamic_relocations_->entry_size},
        {rela ? dt_relacount : dt_relcount, relatives_.size()},
        {dt_symtab, address_of(dynamic_symbols_)},
        {dt_syment, dynamic_symbols_->entry_size},
        {dt_strtab, address_of(dynamic_strings_)},
        {dt_strsz, dynamic_strings_->size},
        {dt_null, 0},
    };
    at = 0;
    for (const auto& [tag, value] : tags) {
        put(*dynamic_, at, tag, word);
        put(*dynamic_, at + word, value, word);
        at += 2 * word;
    }
}

} // namespace cross::linker
