// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "assembler/elf_writer.hpp"

#include <algorithm>

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

void MergedStringTable::finalize(std::uint32_t prefix, std::uint32_t alignment) {
    std::vector<std::pair<const std::string, std::uint32_t>*> order;
    order.reserve(strings_.size());
    for (auto& entry : strings_) order.push_back(&entry);
    // Descending by characters from the end; a string that is a suffix of
    // another follows it.
    std::sort(order.begin(), order.end(), [](const auto* a, const auto* b) {
        const auto& x = a->first;
        const auto& y = b->first;
        for (std::size_t position = 0;; ++position) {
            const int cx = position < x.size() ? static_cast<unsigned char>(x[x.size() - 1 - position]) : -1;
            const int cy = position < y.size() ? static_cast<unsigned char>(y[y.size() - 1 - position]) : -1;
            if (cx != cy) return cx > cy;
            if (cx == -1) return false;
        }
    });
    size_ = prefix;
    std::string_view previous;
    for (auto* entry : order) {
        const std::string_view text = entry->first;
        if (previous.size() >= text.size() &&
            previous.substr(previous.size() - text.size()) == text) {
            entry->second = size_ - static_cast<std::uint32_t>(text.size()) - 1;
            continue;
        }
        entry->second = size_;
        size_ += static_cast<std::uint32_t>(text.size()) + 1;
        previous = text;
    }
    if (alignment > 1) size_ = (size_ + alignment - 1) / alignment * alignment;
}

std::uint32_t MergedStringTable::offset(std::string_view text) const {
    const auto found = strings_.find(std::string(text));
    return found == strings_.end() ? 0 : found->second;
}

std::vector<std::uint8_t> MergedStringTable::data(std::uint32_t prefix) const {
    std::vector<std::uint8_t> bytes(size_ - prefix, 0);
    for (const auto& [text, offset] : strings_) {
        std::copy(text.begin(), text.end(), bytes.begin() + (offset - prefix));
    }
    return bytes;
}

std::vector<std::uint8_t> write_elf(const ObjectInfo& object,
                                    std::uint32_t flags,
                                    std::span<const ElfSection> sections,
                                    std::uint32_t names,
                                    const ElfExecutable* executable) {
    ByteWriter out(object.big_endian);
    const bool wide = object.elf64;
    const unsigned header_size = wide ? 64 : 52;
    const unsigned segment_header_size = wide ? 56 : 32;
    const unsigned section_header_size = wide ? 64 : 40;
    const std::size_t segment_count = executable ? executable->segments.size() : 0;
    auto& data = out.data();
    data.resize(header_size + segment_count * segment_header_size);

    std::vector<std::uint64_t> offsets(sections.size());
    std::uint64_t end = data.size();
    for (std::size_t index = 0; index < sections.size(); ++index) {
        const auto& section = sections[index];
        std::uint64_t offset = end;
        if (section.offset) {
            offset = *section.offset;
        } else {
            if (section.alignment > 1) {
                offset = (offset + section.alignment - 1) / section.alignment * section.alignment;
            }
            end = offset;
        }
        offsets[index] = offset;
        if (section.type == elf::sht_nobits) continue;
        if (data.size() < offset + section.data.size()) data.resize(offset + section.data.size());
        std::copy(section.data.begin(), section.data.end(),
                  data.begin() + static_cast<std::ptrdiff_t>(offset));
        end = std::max<std::uint64_t>(end, offset + section.data.size());
    }
    data.resize(end);
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
            out.u64(section.address);
            out.u64(offsets[index]);
            out.u64(size);
        } else {
            out.u32(section.flags);
            out.u32(section.address);
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
    header.u16(executable ? executable->type : 1); // ET_REL for an object
    header.u16(object.machine);
    header.u32(1);
    const std::uint64_t entry = executable ? executable->entry : 0;
    const std::uint64_t segment_headers = segment_count != 0 ? header_size : 0;
    if (wide) {
        header.u64(entry);
        header.u64(segment_headers);
        header.u64(section_headers);
    } else {
        header.u32(entry);
        header.u32(segment_headers);
        header.u32(section_headers);
    }
    header.u32(flags);
    header.u16(header_size);
    header.u16(segment_count != 0 ? segment_header_size : 0);
    header.u16(segment_count);
    header.u16(section_header_size);
    header.u16(sections.size() + 1);
    header.u16(names);
    if (executable) {
        for (const auto& segment : executable->segments) {
            header.u32(segment.type);
            if (wide) {
                header.u32(segment.flags);
                header.u64(segment.offset);
                header.u64(segment.address);
                header.u64(segment.physical_address);
                header.u64(segment.file_size);
                header.u64(segment.memory_size);
                header.u64(segment.alignment);
            } else {
                header.u32(segment.offset);
                header.u32(segment.address);
                header.u32(segment.physical_address);
                header.u32(segment.file_size);
                header.u32(segment.memory_size);
                header.u32(segment.flags);
                header.u32(segment.alignment);
            }
        }
    }
    std::copy(header.data().begin(), header.data().end(), out.data().begin());
    return std::move(out.data());
}

} // namespace cross::assembler
