// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/elf_writer.hpp"

namespace cross::assembler {

void ByteWriter::integer(std::uint64_t value, unsigned size) {
    for (unsigned index = 0; index < size; ++index) {
        const unsigned shift = big_endian_ ? (size - 1 - index) * 8 : index * 8;
        data_.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void ByteWriter::align(std::uint64_t alignment) {
    while (alignment > 1 && data_.size() % alignment != 0) data_.push_back(0);
}

std::uint32_t StringTable::add(std::string_view text) {
    if (text.empty()) return 0;
    const auto [entry, inserted] =
        offsets_.try_emplace(std::string(text), 0U);
    if (inserted) {
        entry->second = static_cast<std::uint32_t>(data_.size());
        data_.insert(data_.end(), text.begin(), text.end());
        data_.push_back(0);
    }
    return entry->second;
}

std::vector<std::uint8_t> write_elf(const ObjectInfo& object,
                                    std::uint32_t flags,
                                    std::span<const ElfSection> sections,
                                    std::uint32_t names) {
    ByteWriter out(object.big_endian);
    const bool wide = object.elf64;
    const unsigned header_size = wide ? 64 : 52;
    const unsigned section_header_size = wide ? 64 : 40;
    out.data().resize(header_size);

    std::vector<std::uint64_t> offsets(sections.size());
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        out.align(section.alignment);
        offsets[index] = out.size();
        if (section.type != elf::sht_nobits) out.bytes(section.data);
    }
    out.align(wide ? 8 : 4);
    const auto section_headers = out.size();
    for (unsigned index = 0; index < section_header_size; ++index) out.u8(0);
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        const std::uint64_t size = section.type == elf::sht_nobits
                                       ? section.size
                                       : section.data.size();
        out.u32(section.name);
        out.u32(section.type);
        if (wide) {
            out.u64(section.flags);
            out.u64(0);
            out.u64(offsets[index]);
            out.u64(size);
        } else {
            out.u32(section.flags);
            out.u32(0);
            out.u32(offsets[index]);
            out.u32(size);
        }
        out.u32(section.link);
        out.u32(section.info);
        if (wide) {
            out.u64(section.alignment);
            out.u64(section.entry_size);
        } else {
            out.u32(section.alignment);
            out.u32(section.entry_size);
        }
    }

    ByteWriter header(object.big_endian);
    header.u8(0x7f);
    header.u8('E');
    header.u8('L');
    header.u8('F');
    header.u8(wide ? 2 : 1);
    header.u8(object.big_endian ? 2 : 1);
    header.u8(1);
    for (unsigned index = 0; index < 9; ++index) header.u8(0);
    header.u16(1); // ET_REL
    header.u16(object.machine);
    header.u32(1);
    if (wide) {
        header.u64(0);
        header.u64(0);
        header.u64(section_headers);
    } else {
        header.u32(0);
        header.u32(0);
        header.u32(section_headers);
    }
    header.u32(flags);
    header.u16(header_size);
    header.u16(0);
    header.u16(0);
    header.u16(section_header_size);
    header.u16(sections.size() + 1);
    header.u16(names);
    std::copy(header.data().begin(), header.data().end(), out.data().begin());
    return std::move(out.data());
}

} // namespace cross::assembler
