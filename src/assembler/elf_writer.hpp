// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "assembler/assembler.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cross::assembler {

// Appends integers in a chosen byte order.
class ByteWriter {
public:
    explicit ByteWriter(bool big_endian) : big_endian_(big_endian) {}
    void u8(std::uint64_t value) {
        data_.push_back(static_cast<std::uint8_t>(value));
    }
    void u16(std::uint64_t value) { integer(value, 2); }
    void u32(std::uint64_t value) { integer(value, 4); }
    void u64(std::uint64_t value) { integer(value, 8); }
    void integer(std::uint64_t value, unsigned size);
    void bytes(std::span<const std::uint8_t> bytes) {
        data_.insert(data_.end(), bytes.begin(), bytes.end());
    }
    void align(std::uint64_t alignment);
    [[nodiscard]] std::size_t size() const { return data_.size(); }
    [[nodiscard]] std::vector<std::uint8_t>& data() { return data_; }

private:
    bool big_endian_;
    std::vector<std::uint8_t> data_;
};

// A string table that shares the bytes of repeated strings.
class StringTable {
public:
    StringTable() { data_.push_back(0); }
    std::uint32_t add(std::string_view text);
    [[nodiscard]] const std::vector<std::uint8_t>& data() const {
        return data_;
    }

private:
    std::vector<std::uint8_t> data_;
    std::unordered_map<std::string, std::uint32_t> offsets_;
};

// A string table laid out as LLVM's StringTableBuilder does: strings sorted
// by their reversed text, a string that ends another sharing its bytes.
class MergedStringTable {
public:
    void add(std::string_view text) { strings_.emplace(text, 0); }
    // Lays the table out after `prefix` leading bytes, padding its size to
    // `alignment`.
    void finalize(std::uint32_t prefix, std::uint32_t alignment = 1);
    [[nodiscard]] std::uint32_t offset(std::string_view text) const;
    [[nodiscard]] std::uint32_t size() const { return size_; }
    // The table's bytes after the prefix, as offsets count them.
    [[nodiscard]] std::vector<std::uint8_t> data(std::uint32_t prefix) const;

private:
    std::unordered_map<std::string, std::uint32_t> strings_;
    std::uint32_t size_{};
};

struct ElfSection {
    std::uint32_t name{};
    std::uint32_t type{};
    std::uint64_t flags{};
    std::uint64_t alignment{1};
    std::uint64_t entry_size{};
    std::uint32_t link{};
    std::uint32_t info{};
    std::vector<std::uint8_t> data;
    // The size of a NOBITS section, which has no data.
    std::uint64_t size{};
    std::uint64_t address{};
    // The file offset; without one the section follows the previous one at
    // its alignment.
    std::optional<std::uint64_t> offset;
};

struct ElfSegment {
    std::uint32_t type{};
    std::uint32_t flags{};
    std::uint64_t offset{};
    std::uint64_t address{};
    std::uint64_t physical_address{};
    std::uint64_t file_size{};
    std::uint64_t memory_size{};
    std::uint64_t alignment{};
};

// The header facts of an executable, whose program headers follow the ELF
// header.
struct ElfExecutable {
    std::uint64_t entry{};
    std::vector<ElfSegment> segments;
};

// Writes an ELF file: a relocatable file, or an executable when `executable`
// is given. `sections` excludes the null section at index 0; `names` is the
// index of the section-name string table.
[[nodiscard]] std::vector<std::uint8_t> write_elf(
    const ObjectInfo& object, std::uint32_t flags,
    std::span<const ElfSection> sections, std::uint32_t names,
    const ElfExecutable* executable = nullptr);

} // namespace cross::assembler
