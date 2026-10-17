// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/input.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace cross::linker {
namespace {

constexpr std::uint16_t em_mips = 8;
constexpr std::uint32_t sht_symtab_shndx = 18;
constexpr std::uint32_t sht_llvm_addrsig = 0x6fff4c03;
constexpr std::uint32_t grp_comdat = 1;

class Reader {
public:
    Reader(std::span<const std::uint8_t> bytes, bool big_endian) : bytes_(bytes), big_(big_endian) {}

    [[nodiscard]] bool contains(std::uint64_t offset, std::uint64_t size) const {
        return offset <= bytes_.size() && size <= bytes_.size() - offset;
    }
    [[nodiscard]] std::uint64_t integer(std::uint64_t offset, unsigned size) const {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < size; ++index) {
            const unsigned shift = big_ ? (size - 1 - index) * 8 : index * 8;
            value |= std::uint64_t{bytes_[offset + index]} << shift;
        }
        return value;
    }
    [[nodiscard]] std::uint8_t u8(std::uint64_t offset) const { return bytes_[offset]; }
    [[nodiscard]] std::uint64_t u16(std::uint64_t offset) const { return integer(offset, 2); }
    [[nodiscard]] std::uint64_t u32(std::uint64_t offset) const { return integer(offset, 4); }
    [[nodiscard]] std::uint64_t u64(std::uint64_t offset) const { return integer(offset, 8); }

private:
    std::span<const std::uint8_t> bytes_;
    bool big_;
};

struct SectionHeader {
    std::uint32_t name{};
    std::uint32_t type{};
    std::uint64_t flags{};
    std::uint64_t address{};
    std::uint64_t offset{};
    std::uint64_t size{};
    std::uint32_t link{};
    std::uint32_t info{};
    std::uint64_t alignment{};
    std::uint64_t entry_size{};
};

std::string string_at(std::span<const std::uint8_t> bytes, const SectionHeader& table,
                      std::uint64_t offset) {
    if (table.type == elf::sht_nobits || offset >= table.size ||
        table.offset + table.size > bytes.size()) {
        return {};
    }
    const auto* begin = reinterpret_cast<const char*>(bytes.data() + table.offset + offset);
    const auto limit = static_cast<std::size_t>(table.size - offset);
    return std::string(begin, strnlen(begin, limit));
}

std::uint64_t parse_decimal(std::string_view text) {
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') break;
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

std::string trim(std::string_view text) {
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return std::string(text);
}

} // namespace

bool is_elf(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 16 && bytes[0] == 0x7f && bytes[1] == 'E' && bytes[2] == 'L' &&
           bytes[3] == 'F';
}

bool is_archive(std::span<const std::uint8_t> bytes) {
    return bytes.size() >= 8 && std::memcmp(bytes.data(), "!<arch>\n", 8) == 0;
}

std::unique_ptr<InputFile> read_object(std::span<const std::uint8_t> bytes, std::string path,
                                       std::string member, bool any_type,
                                       Diagnostics& diagnostics) {
    auto file = std::make_unique<InputFile>();
    file->path = std::move(path);
    file->member = std::move(member);
    const auto fail = [&](std::string_view message) -> std::unique_ptr<InputFile> {
        diagnostics.command_error(file->name() + ": " + std::string(message));
        return nullptr;
    };
    if (!is_elf(bytes)) return fail("not an ELF file");
    if ((bytes[4] != 1 && bytes[4] != 2) || (bytes[5] != 1 && bytes[5] != 2)) {
        return fail("unsupported ELF class or byte order");
    }
    const bool wide = bytes[4] == 2;
    const bool big = bytes[5] == 2;
    const Reader in(bytes, big);
    const unsigned header_size = wide ? 64 : 52;
    if (!in.contains(0, header_size)) return fail("truncated ELF header");
    const auto type = in.u16(16);
    file->object.elf64 = wide;
    file->object.big_endian = big;
    file->object.machine = static_cast<std::uint16_t>(in.u16(18));
    if (!any_type && type != 1) return fail("not a relocatable object");
    std::uint64_t section_offset = wide ? in.u64(40) : in.u32(32);
    file->flags = static_cast<std::uint32_t>(in.u32(wide ? 48 : 36));
    const auto entry_size = in.u16(wide ? 58 : 46);
    std::uint64_t count = in.u16(wide ? 60 : 48);
    std::uint64_t names = in.u16(wide ? 62 : 50);
    const unsigned header_entry = wide ? 64 : 40;
    if (section_offset == 0) return file;
    if (entry_size != header_entry || !in.contains(section_offset, header_entry)) {
        return fail("invalid section header table");
    }
    const auto read_header = [&](std::uint64_t index) {
        const auto at = section_offset + index * header_entry;
        SectionHeader header;
        header.name = static_cast<std::uint32_t>(in.u32(at));
        header.type = static_cast<std::uint32_t>(in.u32(at + 4));
        if (wide) {
            header.flags = in.u64(at + 8);
            header.address = in.u64(at + 16);
            header.offset = in.u64(at + 24);
            header.size = in.u64(at + 32);
            header.link = static_cast<std::uint32_t>(in.u32(at + 40));
            header.info = static_cast<std::uint32_t>(in.u32(at + 44));
            header.alignment = in.u64(at + 48);
            header.entry_size = in.u64(at + 56);
        } else {
            header.flags = in.u32(at + 8);
            header.address = in.u32(at + 12);
            header.offset = in.u32(at + 16);
            header.size = in.u32(at + 20);
            header.link = static_cast<std::uint32_t>(in.u32(at + 24));
            header.info = static_cast<std::uint32_t>(in.u32(at + 28));
            header.alignment = in.u32(at + 32);
            header.entry_size = in.u32(at + 36);
        }
        return header;
    };
    const auto first = read_header(0);
    if (count == 0) count = first.size;
    if (names == 0xffff) names = first.link;
    if (!in.contains(section_offset, count * header_entry)) return fail("invalid section header table");
    std::vector<SectionHeader> headers;
    headers.reserve(count);
    for (std::uint64_t index = 0; index < count; ++index) {
        headers.push_back(read_header(index));
        const auto& header = headers.back();
        if (header.type != elf::sht_nobits && header.type != 0 &&
            !in.contains(header.offset, header.size)) {
            return fail("section " + std::to_string(index) + " lies outside the file");
        }
    }
    if (names >= count) return fail("invalid section name table index");
    const auto& name_table = headers[names];
    const auto section_name = [&](const SectionHeader& header) {
        return string_at(bytes, name_table, header.name);
    };

    // Symbols.
    std::optional<std::uint64_t> symtab;
    for (std::uint64_t index = 0; index < count; ++index) {
        if (headers[index].type == elf::sht_symtab) {
            if (symtab) return fail("more than one symbol table");
            symtab = index;
        }
        if (headers[index].type == sht_symtab_shndx) {
            return fail("extended section indices are not supported");
        }
    }
    if (symtab) {
        const auto& table = headers[*symtab];
        const unsigned size = wide ? 24 : 16;
        if (table.entry_size != size || table.link >= count) return fail("invalid symbol table");
        const auto& strings = headers[table.link];
        const auto symbols = table.size / size;
        file->symbols.reserve(symbols);
        for (std::uint64_t index = 0; index < symbols; ++index) {
            const auto at = table.offset + index * size;
            FileSymbol symbol;
            std::uint8_t info = 0;
            if (wide) {
                symbol.name = string_at(bytes, strings, in.u32(at));
                info = in.u8(at + 4);
                symbol.other = in.u8(at + 5);
                symbol.shndx = static_cast<std::uint32_t>(in.u16(at + 6));
                symbol.value = in.u64(at + 8);
                symbol.size = in.u64(at + 16);
            } else {
                symbol.name = string_at(bytes, strings, in.u32(at));
                symbol.value = in.u32(at + 4);
                symbol.size = in.u32(at + 8);
                info = in.u8(at + 12);
                symbol.other = in.u8(at + 13);
                symbol.shndx = static_cast<std::uint32_t>(in.u16(at + 14));
            }
            symbol.type = info & 0xfU;
            symbol.binding = static_cast<std::uint8_t>(info >> 4U);
            if (symbol.type == elf::stt_section && symbol.name.empty() && symbol.shndx < count) {
                symbol.name = section_name(headers[symbol.shndx]);
            }
            file->symbols.push_back(std::move(symbol));
        }
    }

    // Sections.
    file->sections.resize(count);
    for (std::uint64_t index = 1; index < count; ++index) {
        const auto& header = headers[index];
        switch (header.type) {
        case 0:
        case elf::sht_symtab:
        case elf::sht_strtab:
        case elf::sht_rel:
        case elf::sht_rela:
        case sht_llvm_addrsig:
            continue;
        case elf::sht_group: {
            if (header.size < 4 || header.size % 4 != 0 || header.info >= file->symbols.size()) {
                return fail("invalid section group");
            }
            Group group;
            group.signature = file->symbols[header.info].name;
            group.symbol = header.info;
            group.comdat = (in.u32(header.offset) & grp_comdat) != 0;
            for (std::uint64_t at = 4; at < header.size; at += 4) {
                group.members.push_back(static_cast<std::uint32_t>(in.u32(header.offset + at)));
            }
            file->groups.push_back(std::move(group));
            continue;
        }
        default:
            break;
        }
        auto name = section_name(header);
        // Executable-stack notes and excluded sections never reach an image.
        if (name == ".note.GNU-stack") {
            file->stack_note = true;
            file->executable_stack = (header.flags & elf::shf_execinstr) != 0;
            continue;
        }
        if ((header.flags & elf::shf_exclude) != 0) continue;
        auto section = std::make_unique<InputSection>();
        section->file = file.get();
        section->name = std::move(name);
        section->type = header.type;
        section->flags = header.flags;
        section->alignment = std::max<std::uint64_t>(header.alignment, 1);
        section->entry_size = header.entry_size;
        section->size = header.size;
        if (header.type != elf::sht_nobits) {
            const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(header.offset);
            section->data.assign(begin, begin + static_cast<std::ptrdiff_t>(header.size));
        }
        file->sections[index] = std::move(section);
    }
    for (std::size_t index = 0; index < file->groups.size(); ++index) {
        for (const auto section : file->groups[index].members) {
            if (section < count && file->sections[section]) {
                file->sections[section]->group = static_cast<std::uint32_t>(index);
            }
        }
    }

    // Relocations.
    for (std::uint64_t index = 1; index < count; ++index) {
        const auto& header = headers[index];
        if (header.type != elf::sht_rel && header.type != elf::sht_rela) continue;
        if (header.info >= count) return fail("invalid relocation section");
        auto* target = file->sections[header.info].get();
        if (!target) continue;
        const bool rela = header.type == elf::sht_rela;
        const unsigned size = wide ? (rela ? 24 : 16) : (rela ? 12 : 8);
        if (header.entry_size != size) return fail("invalid relocation entry size");
        target->rela = rela;
        const auto entries = header.size / size;
        target->relocations.reserve(entries);
        for (std::uint64_t entry = 0; entry < entries; ++entry) {
            const auto at = header.offset + entry * size;
            assembler::Relocation relocation;
            std::uint64_t symbol = 0;
            if (wide) {
                relocation.offset = in.u64(at);
                if (file->object.machine == em_mips) {
                    symbol = in.u32(at + 8);
                    relocation.type = static_cast<std::uint32_t>(
                        in.u8(at + 15) | (in.u8(at + 14) << 8U) | (in.u8(at + 13) << 16U));
                } else {
                    const auto info = in.u64(at + 8);
                    symbol = info >> 32U;
                    relocation.type = static_cast<std::uint32_t>(info);
                }
                if (rela) relocation.addend = static_cast<std::int64_t>(in.u64(at + 16));
            } else {
                relocation.offset = in.u32(at);
                const auto info = in.u32(at + 4);
                symbol = info >> 8U;
                relocation.type = static_cast<std::uint32_t>(info & 0xffU);
                if (rela) {
                    relocation.addend = static_cast<std::int32_t>(static_cast<std::uint32_t>(in.u32(at + 8)));
                }
            }
            if (symbol >= file->symbols.size() && !(symbol == 0 && file->symbols.empty())) {
                return fail("relocation names a symbol outside the symbol table");
            }
            relocation.symbol = assembler::SymbolId{static_cast<std::uint32_t>(symbol)};
            target->relocations.push_back(relocation);
        }
    }
    return file;
}

std::optional<Archive> read_archive(std::vector<std::uint8_t> bytes, std::string path,
                                    Diagnostics& diagnostics) {
    Archive archive;
    archive.path = std::move(path);
    archive.bytes = std::move(bytes);
    const auto& data = archive.bytes;
    const auto fail = [&](std::string_view message) -> std::optional<Archive> {
        diagnostics.command_error(archive.path + ": " + std::string(message));
        return std::nullopt;
    };
    std::string long_names;
    std::vector<std::pair<std::string, std::uint64_t>> index;
    // The member index of each member header offset.
    std::unordered_map<std::uint64_t, std::size_t> members;
    bool has_index = false;
    std::size_t at = 8;
    while (at < data.size()) {
        if (at + 60 > data.size()) return fail("truncated archive member header");
        const auto* header = reinterpret_cast<const char*>(data.data() + at);
        if (header[58] != '`' || header[59] != '\n') return fail("invalid archive member header");
        const std::string_view raw_name(header, 16);
        const auto size = parse_decimal(trim(std::string_view(header + 48, 10)));
        const auto body = at + 60;
        if (size > data.size() - body) return fail("truncated archive member");
        const Reader big_endian(data, true);
        if (raw_name.starts_with("/               ") || raw_name.starts_with("/SYM64/")) {
            const bool wide = raw_name.starts_with("/SYM64/");
            const unsigned word = wide ? 8 : 4;
            if (size < word) return fail("invalid archive symbol index");
            const auto symbols = big_endian.integer(body, word);
            if (symbols > (size - word) / word) return fail("invalid archive symbol index");
            auto names = body + word + symbols * word;
            for (std::uint64_t symbol = 0; symbol < symbols; ++symbol) {
                const auto offset = big_endian.integer(body + word + symbol * word, word);
                const auto* begin = reinterpret_cast<const char*>(data.data() + names);
                const auto limit = body + size - names;
                const auto length = strnlen(begin, static_cast<std::size_t>(limit));
                if (length == limit) return fail("invalid archive symbol index");
                index.emplace_back(std::string(begin, length), offset);
                names += length + 1;
            }
            has_index = true;
        } else if (raw_name.starts_with("//              ")) {
            long_names.assign(reinterpret_cast<const char*>(data.data() + body), size);
        } else {
            std::string name;
            if (raw_name.starts_with("/") && raw_name.size() > 1 &&
                std::isdigit(static_cast<unsigned char>(raw_name[1]))) {
                const auto offset = parse_decimal(raw_name.substr(1));
                if (offset >= long_names.size()) return fail("invalid archive member name");
                const auto end = long_names.find_first_of("/\n", offset);
                name = long_names.substr(offset, end == std::string::npos ? end : end - offset);
            } else {
                name = trim(raw_name);
                if (name.size() > 1 && name.back() == '/') name.pop_back();
            }
            members[at] = archive.members.size();
            archive.members.push_back({std::move(name), body, static_cast<std::size_t>(size), false});
        }
        at = body + size + (size & 1U);
    }
    if (!archive.members.empty() && !has_index) {
        return fail("archive has no symbol index; run ranlib to add one");
    }
    for (const auto& [name, header] : index) {
        const auto member = members.find(header);
        if (member == members.end()) return fail("archive symbol index names no member");
        archive.symbols.emplace_back(name, member->second);
    }
    return archive;
}

std::unique_ptr<InputFile> binary_object(std::vector<std::uint8_t> bytes, std::string path,
                                         const assembler::ObjectInfo& object) {
    auto file = std::make_unique<InputFile>();
    file->path = std::move(path);
    file->object = object;
    file->raw = true;
    auto section = std::make_unique<InputSection>();
    section->file = file.get();
    section->name = ".data";
    section->flags = elf::shf_alloc | elf::shf_write;
    section->size = bytes.size();
    section->data = std::move(bytes);
    std::string stem = "_binary_";
    for (const char c : file->path) {
        stem += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    }
    const auto size = section->size;
    file->symbols.push_back({});
    file->symbols.push_back({stem + "_start", 0, 0, elf::stt_notype, elf::stb_global, 0, 1, nullptr});
    file->symbols.push_back({stem + "_end", size, 0, elf::stt_notype, elf::stb_global, 0, 1, nullptr});
    file->symbols.push_back({stem + "_size", size, 0, elf::stt_notype, elf::stb_global, 0, 0xfff1, nullptr});
    file->sections.resize(2);
    file->sections[1] = std::move(section);
    return file;
}

} // namespace cross::linker
