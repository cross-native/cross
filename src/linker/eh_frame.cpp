// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/link.hpp"

#include <algorithm>
#include <cstring>

// .eh_frame editing as GNU ld does it (bfd/elf-eh-frame.c): CIEs and FDEs
// are parsed, FDEs of removed code and empty ranges are dropped, identical
// CIEs share one copy, trailing DW_CFA_nop padding is trimmed and entries
// realigned, and --eh-frame-hdr builds the binary search table.

namespace cross::linker {
namespace {

constexpr std::uint8_t pe_absptr = 0x00;
constexpr std::uint8_t pe_omit = 0xff;
constexpr std::uint8_t pe_udata2 = 0x02;
constexpr std::uint8_t pe_udata4 = 0x03;
constexpr std::uint8_t pe_udata8 = 0x04;
constexpr std::uint8_t pe_signed = 0x08;
constexpr std::uint8_t pe_pcrel = 0x10;
constexpr std::uint8_t pe_datarel = 0x30;
constexpr std::uint8_t pe_aligned = 0x50;
constexpr std::uint8_t pe_sdata4 = 0x0b;

constexpr std::uint8_t cfa_nop = 0x00;
constexpr std::uint8_t cfa_set_loc = 0x01;

// The width of a pointer encoding; zero when it is variable or unknown.
unsigned encoding_width(std::uint8_t encoding, unsigned pointer) {
    if ((encoding & 0x60) == 0x60) return 0;
    switch (encoding & 7) {
    case pe_udata2: return 2;
    case pe_udata4: return 4;
    case pe_udata8: return 8;
    case pe_absptr: return pointer;
    default: return 0;
    }
}

class Cursor {
public:
    Cursor(const std::vector<std::uint8_t>& data, std::uint64_t at, std::uint64_t end)
        : data_(data), at_(at), end_(end) {}
    [[nodiscard]] std::uint64_t at() const { return at_; }
    [[nodiscard]] std::uint64_t end() const { return end_; }
    bool byte(std::uint8_t& value) {
        if (at_ >= end_) return false;
        value = data_[at_++];
        return true;
    }
    bool skip(std::uint64_t count) {
        if (end_ - at_ < count) {
            at_ = end_;
            return false;
        }
        at_ += count;
        return true;
    }
    bool skip_leb128() {
        std::uint8_t value = 0;
        do {
            if (!byte(value)) return false;
        } while ((value & 0x80) != 0);
        return true;
    }
    bool uleb128(std::uint64_t& value) {
        value = 0;
        unsigned shift = 0;
        std::uint8_t byte_value = 0;
        do {
            if (!byte(byte_value)) return false;
            if (shift < 64) value |= std::uint64_t{byte_value & 0x7fU} << shift;
            shift += 7;
        } while ((byte_value & 0x80) != 0);
        return true;
    }
    bool sleb128(std::int64_t& value) {
        std::uint64_t result = 0;
        unsigned shift = 0;
        std::uint8_t byte_value = 0;
        do {
            if (!byte(byte_value)) return false;
            if (shift < 64) result |= std::uint64_t{byte_value & 0x7fU} << shift;
            shift += 7;
        } while ((byte_value & 0x80) != 0);
        if (shift < 64 && (byte_value & 0x40) != 0) result |= ~std::uint64_t{0} << shift;
        value = static_cast<std::int64_t>(result);
        return true;
    }

private:
    const std::vector<std::uint8_t>& data_;
    std::uint64_t at_;
    std::uint64_t end_;
};

// Skips one call frame instruction (GNU ld's skip_cfa_op).
bool skip_instruction(Cursor& cursor, unsigned pointer_width) {
    std::uint8_t op = 0;
    if (!cursor.byte(op)) return false;
    const auto high = op & 0xc0U;
    switch (high ? high : op) {
    case 0x00: // nop
    case 0x40: // advance_loc
    case 0xc0: // restore
    case 0x0a: // remember_state
    case 0x0b: // restore_state
    case 0x2d: // GNU_window_save
        return true;
    case 0x80: // offset
    case 0x06: // restore_extended
    case 0x07: // undefined
    case 0x08: // same_value
    case 0x0d: // def_cfa_register
    case 0x0e: // def_cfa_offset
    case 0x13: // def_cfa_offset_sf
    case 0x2e: // GNU_args_size
        return cursor.skip_leb128();
    case 0x14: // val_offset
    case 0x15: // val_offset_sf
    case 0x05: // offset_extended
    case 0x09: // register
    case 0x0c: // def_cfa
    case 0x11: // offset_extended_sf
    case 0x2f: // GNU_negative_offset_extended
    case 0x12: // def_cfa_sf
        return cursor.skip_leb128() && cursor.skip_leb128();
    case 0x0f: { // def_cfa_expression
        std::uint64_t length = 0;
        return cursor.uleb128(length) && cursor.skip(length);
    }
    case 0x10: // expression
    case 0x16: { // val_expression
        std::uint64_t length = 0;
        return cursor.skip_leb128() && cursor.uleb128(length) && cursor.skip(length);
    }
    case 0x01: // set_loc
        return cursor.skip(pointer_width);
    case 0x02: return cursor.skip(1); // advance_loc1
    case 0x03: return cursor.skip(2); // advance_loc2
    case 0x04: return cursor.skip(4); // advance_loc4
    case 0x1d: return cursor.skip(8); // MIPS_advance_loc8
    default: return false;
    }
}

// The offset of the first DW_CFA_nop padding byte after the instructions
// in [begin, end), counting DW_CFA_set_loc; nullopt when an instruction is
// not understood.
std::optional<std::uint64_t> instructions_end(const std::vector<std::uint8_t>& data, std::uint64_t begin,
                                              std::uint64_t end, unsigned pointer_width,
                                              unsigned& set_locs) {
    Cursor cursor(data, begin, end);
    std::uint64_t last = begin;
    while (cursor.at() < end) {
        if (data[cursor.at()] == cfa_nop) {
            cursor.skip(1);
            continue;
        }
        if (data[cursor.at()] == cfa_set_loc) ++set_locs;
        if (!skip_instruction(cursor, pointer_width)) return std::nullopt;
        last = cursor.at();
    }
    return last;
}

} // namespace

bool Link::parse_frame_section(InputSection& section) {
    // GNU ld's _bfd_elf_parse_eh_frame: a section it does not understand
    // stays whole, and no search table is built.
    const auto& data = section.data;
    const auto& relocations = section.relocations;
    const unsigned pointer = object_.elf64 ? 8 : 4;
    for (std::size_t index = 1; index < relocations.size(); ++index) {
        if (relocations[index].offset < relocations[index - 1].offset) return false;
    }
    const auto u32 = [&](std::uint64_t at) {
        std::uint32_t value = 0;
        for (unsigned index = 0; index < 4; ++index) {
            const unsigned shift = object_.big_endian ? (3 - index) * 8 : index * 8;
            value |= std::uint32_t{data[at + index]} << shift;
        }
        return value;
    };
    const auto read_value = [&](std::uint64_t at, unsigned width) {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < width; ++index) {
            const unsigned shift = object_.big_endian ? (width - 1 - index) * 8 : index * 8;
            value |= std::uint64_t{data[at + index]} << shift;
        }
        return value;
    };
    std::size_t next = 0;
    // No relocation may precede `at` but those consumed (R_*_NONE aside).
    const auto no_relocations = [&](std::uint64_t at) {
        while (next < relocations.size() && relocations[next].offset < at) {
            if (relocations[next].type != 0 || relocations[next].symbol.value != 0) return false;
            ++next;
        }
        return true;
    };
    const auto skip_relocations = [&](std::uint64_t at) {
        while (next < relocations.size() && relocations[next].offset < at) ++next;
    };
    const auto relocation_at = [&](std::uint64_t at) {
        return next < relocations.size() && relocations[next].offset == at;
    };
    FrameSection parsed;
    parsed.original_size = section.size;
    std::uint64_t at = 0;
    const auto size = data.size();
    while (at != size) {
        FrameEntry entry;
        entry.offset = at;
        entry.first_relocation = next;
        if (size - at < 4) return false;
        const auto length = u32(at);
        if (length == 0xffffffffU || length > size - at - 4) return false;
        const auto end = at + 4 + length;
        entry.size = 4 + length;
        if (length == 0) {
            // Zero terminators come last.
            for (std::uint64_t word = at; word < size; word += 4) {
                if (size - word < 4 || u32(word) != 0) return false;
            }
            if (!no_relocations(size)) return false;
            entry.kind = FrameEntry::Kind::Terminator;
            entry.end_relocation = next;
            parsed.entries.push_back(std::move(entry));
            break;
        }
        if (length < 4) return false;
        const auto id = u32(at + 4);
        Cursor cursor(data, at + 8, end);
        std::uint64_t instructions = 0;
        unsigned set_locs = 0;
        if (id == 0) {
            entry.kind = FrameEntry::Kind::Cie;
            std::uint8_t version = 0;
            if (!cursor.byte(version) || (version != 1 && version != 3 && version != 4)) return false;
            const auto nul = std::find(data.begin() + static_cast<std::ptrdiff_t>(cursor.at()),
                                       data.begin() + static_cast<std::ptrdiff_t>(end), 0);
            if (nul == data.begin() + static_cast<std::ptrdiff_t>(end)) return false;
            const std::string augmentation(data.begin() + static_cast<std::ptrdiff_t>(cursor.at()), nul);
            if (augmentation.size() >= 20) return false;
            cursor.skip(augmentation.size() + 1);
            if (end - cursor.at() < 2) return false;
            if (!no_relocations(cursor.at())) return false;
            bool mergeable = true;
            if (augmentation.starts_with("eh")) {
                // GCC 2 CIEs keep a private exception table pointer.
                if (!cursor.skip(pointer)) return false;
                skip_relocations(cursor.at());
                mergeable = false;
            }
            if (version >= 4) {
                std::uint8_t address_size = 0;
                std::uint8_t segment_size = 0;
                if (!cursor.byte(address_size) || !cursor.byte(segment_size) || address_size != pointer ||
                    segment_size != 0) {
                    return false;
                }
            }
            std::uint64_t code_align = 0;
            std::int64_t data_align = 0;
            std::uint64_t return_column = 0;
            if (!cursor.uleb128(code_align) || !cursor.sleb128(data_align)) return false;
            if (version == 1) {
                std::uint8_t column = 0;
                if (!cursor.byte(column)) return false;
                return_column = column;
            } else if (!cursor.uleb128(return_column)) {
                return false;
            }
            if (!no_relocations(cursor.at())) return false;
            std::uint8_t lsda = pe_omit;
            std::uint8_t fde = pe_omit;
            std::uint8_t personality = pe_omit;
            std::uint64_t augmentation_size = 0;
            std::string identity;
            if (!augmentation.starts_with("eh")) {
                std::size_t position = 0;
                if (!augmentation.empty() && augmentation[0] == 'z') {
                    ++position;
                    if (!cursor.uleb128(augmentation_size) || !no_relocations(cursor.at())) return false;
                }
                for (; position < augmentation.size(); ++position) {
                    switch (augmentation[position]) {
                    case 'L':
                        if (!cursor.byte(lsda) || !no_relocations(cursor.at()) || encoding_width(lsda, pointer) == 0) {
                            return false;
                        }
                        break;
                    case 'R':
                        if (!cursor.byte(fde) || !no_relocations(cursor.at()) || encoding_width(fde, pointer) == 0) {
                            return false;
                        }
                        break;
                    case 'S':
                        break;
                    case 'P': {
                        if (!cursor.byte(personality)) return false;
                        const auto width = encoding_width(personality, pointer);
                        if (width == 0) return false;
                        if ((personality & 0x70) == pe_aligned) {
                            if (!cursor.skip((~cursor.at() + 1) & (width - 1))) return false;
                            if (width == 8) entry.aligned8 = true;
                        }
                        if (!no_relocations(cursor.at()) || !relocation_at(cursor.at())) return false;
                        // The personality routine's identity: the global symbol,
                        // or the file's local symbol and its section.
                        const auto symbol = relocations[next].symbol.value;
                        while (relocation_at(cursor.at())) ++next;
                        if (!cursor.skip(width)) return false;
                        const auto& file = *section.file;
                        if (symbol == 0 || symbol >= file.symbols.size()) {
                            mergeable = false;
                        } else if (const auto* global = file.symbols[symbol].global) {
                            identity = "g" + std::to_string(reinterpret_cast<std::uintptr_t>(global));
                        } else {
                            const auto shndx = file.symbols[symbol].shndx;
                            const auto* target = shndx < file.sections.size() ? file.sections[shndx].get() : nullptr;
                            if (!target || !target->included()) {
                                mergeable = false;
                            } else {
                                identity = "l" + std::to_string(reinterpret_cast<std::uintptr_t>(&file)) + ":" +
                                           std::to_string(symbol);
                            }
                        }
                        break;
                    }
                    default:
                        return false;
                    }
                }
            }
            if (fde == pe_omit) fde = pe_absptr;
            entry.fde_encoding = fde;
            instructions = cursor.at();
            if (!no_relocations(end)) return false;
            const auto trimmed = instructions_end(data, instructions, end, encoding_width(fde, pointer), set_locs);
            if (!trimmed || set_locs != 0) return false;
            entry.size -= end - *trimmed;
            const auto insns = *trimmed - instructions;
            // GNU ld's merge key: everything but the bytes of pointers.
            if (mergeable && insns <= 50) {
                std::string key;
                const auto put = [&](std::uint64_t value) {
                    key.append(reinterpret_cast<const char*>(&value), sizeof value);
                };
                put(entry.size - 4);
                put(version);
                key += augmentation;
                key.push_back('\0');
                put(code_align);
                put(static_cast<std::uint64_t>(data_align));
                put(return_column);
                put(augmentation_size);
                key += identity;
                key.push_back('\0');
                put(personality);
                put(lsda);
                put(fde);
                put(insns);
                key.append(data.begin() + static_cast<std::ptrdiff_t>(instructions),
                           data.begin() + static_cast<std::ptrdiff_t>(*trimmed));
                entry.key = std::move(key);
            }
        } else {
            entry.kind = FrameEntry::Kind::Fde;
            const auto cie_offset = at + 4 - id;
            std::optional<std::size_t> cie;
            for (std::size_t index = 0; index < parsed.entries.size(); ++index) {
                if (parsed.entries[index].kind == FrameEntry::Kind::Cie && parsed.entries[index].offset == cie_offset) {
                    cie = index;
                }
            }
            if (!cie) return false;
            const auto& owner = parsed.entries[*cie];
            entry.fde_encoding = owner.fde_encoding;
            if (!no_relocations(cursor.at()) || !relocation_at(cursor.at())) return false;
            entry.pc_relocation = next;
            const auto width = encoding_width(owner.fde_encoding, pointer);
            if (!cursor.skip(2 * width)) return false;
            skip_relocations(cursor.at() - width);
            // An FDE with an empty range and no relocation for it goes.
            if (!relocation_at(cursor.at() - width) && read_value(cursor.at() - width, width) == 0) {
                entry.cie.reset();
            } else {
                entry.cie = cie;
            }
            // Skip the augmentation data (an LSDA pointer at most).
            std::uint64_t augmentation_length = 0;
            if (data[owner.offset + 9] == 'z' && !cursor.uleb128(augmentation_length)) return false;
            if (!cursor.skip(augmentation_length)) return false;
            instructions = cursor.at();
            skip_relocations(end);
            const auto trimmed = instructions_end(data, instructions, end, width, set_locs);
            if (!trimmed) return false;
            // DW_CFA_set_loc operands move with their relocations, but a
            // PC-relative one without a relocation would need adjusting.
            if (set_locs != 0 && (owner.fde_encoding & 0x70) == pe_pcrel) return false;
            entry.size -= end - *trimmed;
            // The code the FDE describes must be in this file.
            const auto symbol = relocations[entry.pc_relocation].symbol.value;
            auto* code = defining_section(*section.file, symbol);
            if (code && code->file != section.file) return false;
            if (code) fdes_[code].emplace_back(&section, parsed.entries.size());
        }
        entry.end_relocation = next;
        while (entry.end_relocation < relocations.size() && relocations[entry.end_relocation].offset < end) {
            ++entry.end_relocation;
        }
        next = entry.end_relocation;
        parsed.entries.push_back(std::move(entry));
        at = end;
    }
    frames_[&section] = std::move(parsed);
    return true;
}

void Link::parse_frames() {
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included() || section->name != ".eh_frame" || section->nobits() ||
                section->size == 0) {
                continue;
            }
            if (!parse_frame_section(*section)) {
                frames_.erase(section.get());
                frame_table_ = false;
                diagnostics_.warning({}, section->location(0) + ": cannot edit .eh_frame; it stays whole and " +
                                             "no .eh_frame_hdr table is built");
            }
        }
    }
}

void Link::mark_frames(InputSection& section, const std::function<void(InputSection*)>& mark) {
    // The FDEs of live code keep what they refer to (LSDAs), and their CIEs
    // keep the personality routines.
    const auto found = fdes_.find(&section);
    if (found == fdes_.end()) return;
    for (const auto& [frame, index] : found->second) {
        auto& entries = frames_[frame].entries;
        const auto mark_entry = [&](const FrameEntry& entry) {
            for (auto relocation = entry.first_relocation; relocation < entry.end_relocation; ++relocation) {
                mark(defining_section(*frame->file, frame->relocations[relocation].symbol.value));
            }
        };
        mark_entry(entries[index]);
        const auto cie = entries[index].cie.value_or(entries.size());
        if (cie < entries.size()) mark_entry(entries[cie]);
    }
}

void Link::edit_frames(bool relocatable) {
    // The .eh_frame output section's parsed input sections, in order, and
    // whether one holds more than a terminator (which keeps .eh_frame_hdr).
    std::vector<InputSection*> sections;
    std::uint64_t alignment = 1;
    bool present = false;
    const auto add = [&](InputSection* section) {
        if (!section->included()) return;
        alignment = std::max(alignment, section->alignment);
        if (section->size > 8) present = true;
        if (frames_.contains(section)) sections.push_back(section);
    };
    if (relocatable) {
        for (const auto& file : files_) {
            for (const auto& section : file->sections) {
                if (section && section->name == ".eh_frame" && !section->group) add(section.get());
            }
        }
    } else {
        for (const auto& placement : placements_) {
            if (!placement->output || placement->output->name != ".eh_frame") continue;
            for (const auto& item : placement->items) {
                for (auto* section : item.sections) add(section);
            }
        }
    }
    const unsigned pointer = object_.elf64 ? 8 : 4;
    std::unordered_map<std::string, std::pair<const InputSection*, std::size_t>> merged;
    for (std::size_t position = 0; position < sections.size(); ++position) {
        auto* section = sections[position];
        auto& frame = frames_[section];
        auto& entries = frame.entries;
        for (std::size_t index = 0; index < entries.size(); ++index) {
            auto& entry = entries[index];
            if (entry.kind == FrameEntry::Kind::Terminator) {
                // Only the last section keeps a terminator.
                entry.removed = position + 1 != sections.size();
                continue;
            }
            if (entry.kind != FrameEntry::Kind::Fde || !entry.cie) continue;
            const auto& relocation = section->relocations[entry.pc_relocation];
            if (foreign(*section->file, relocation.symbol.value)) continue;
            entry.removed = false;
            // The CIE kept for the FDE: its own, or an identical earlier one.
            auto& cie = entries[*entry.cie];
            if (cie.merged_section) {
                entry.kept_section = cie.merged_section;
                entry.kept_offset = cie.merged_index;
            } else if (!cie.removed) {
                entry.kept_section = section;
                entry.kept_offset = *entry.cie;
            } else {
                cie.removed = false;
                entry.kept_section = section;
                entry.kept_offset = *entry.cie;
                if (!relocatable && !cie.key.empty()) {
                    auto key = cie.key;
                    const auto* output = section->output;
                    key.append(reinterpret_cast<const char*>(&output), sizeof output);
                    const auto [existing, fresh] = merged.emplace(std::move(key), std::make_pair(section, *entry.cie));
                    if (!fresh) {
                        cie.removed = true;
                        cie.merged_section = existing->second.first;
                        cie.merged_index = existing->second.second;
                        entry.kept_section = cie.merged_section;
                        entry.kept_offset = cie.merged_index;
                    }
                }
            }
        }
        // New offsets: CIEs 4-aligned (8 for an 8-byte aligned personality
        // pointer), FDEs by their address width.
        std::uint64_t offset = 0;
        for (auto& entry : entries) {
            if (entry.removed) continue;
            std::uint64_t align = 4;
            if (entry.kind == FrameEntry::Kind::Cie) {
                if (entry.aligned8) align = 8;
            } else if (entry.kind == FrameEntry::Kind::Fde) {
                align = std::max<std::uint64_t>(4, [&] {
                    switch (entry.fde_encoding & 7) {
                    case pe_udata2: return 2U;
                    case pe_udata4: return 4U;
                    case pe_udata8: return 8U;
                    case pe_absptr: return pointer;
                    default: return 4U;
                    }
                }());
            }
            offset = (offset + align - 1) / align * align;
            entry.new_offset = offset;
            offset += entry.size;
        }
        section->size = (offset + 3) / 4 * 4;
    }
    // Empty sections at the end take no alignment padding; the others but
    // the last non-empty one pad their last entry to the output alignment.
    std::size_t last = sections.size();
    while (last > 0) {
        auto* section = sections[last - 1];
        if (section->size == 0) {
            section->live = false;
        } else if (section->size > 4) {
            break;
        }
        --last;
    }
    for (std::size_t position = 0; position + 1 < last; ++position) {
        auto* section = sections[position];
        section->size = (section->size + alignment - 1) / alignment * alignment;
    }
    // Rewrite each section's contents and relocations.
    for (auto* section : sections) {
        auto& frame = frames_[section];
        auto& entries = frame.entries;
        std::vector<std::uint8_t> data(section->size, 0);
        std::vector<assembler::Relocation> relocations;
        const auto put32 = [&](std::uint64_t at, std::uint64_t value) {
            for (unsigned index = 0; index < 4; ++index) {
                const unsigned shift = object_.big_endian ? (3 - index) * 8 : index * 8;
                data[at + index] = static_cast<std::uint8_t>(value >> shift);
            }
        };
        for (std::size_t index = 0; index < entries.size(); ++index) {
            const auto& entry = entries[index];
            if (entry.removed) continue;
            std::copy_n(section->data.begin() + static_cast<std::ptrdiff_t>(entry.offset), entry.size,
                        data.begin() + static_cast<std::ptrdiff_t>(entry.new_offset));
            for (auto relocation = entry.first_relocation; relocation < entry.end_relocation; ++relocation) {
                auto moved = section->relocations[relocation];
                if (moved.offset >= entry.offset + entry.size) continue;
                moved.offset = moved.offset - entry.offset + entry.new_offset;
                relocations.push_back(moved);
            }
            if (entry.kind == FrameEntry::Kind::Terminator) continue;
            std::uint64_t next = section->size;
            for (auto other = index + 1; other < entries.size(); ++other) {
                if (!entries[other].removed) {
                    next = entries[other].new_offset;
                    break;
                }
            }
            put32(entry.new_offset, next - entry.new_offset - 4);
        }
        // CIE pointers within the section; others follow after layout.
        for (auto& entry : entries) {
            if (entry.removed || entry.kind != FrameEntry::Kind::Fde) continue;
            const auto cie_offset = frames_[entry.kept_section].entries[entry.kept_offset].new_offset;
            entry.kept_offset = cie_offset;
            if (entry.kept_section == section) put32(entry.new_offset + 4, entry.new_offset + 4 - cie_offset);
        }
        // Symbols in the section move with their entries; one in a removed
        // entry moves as far as that entry's start does to the next kept
        // entry (GNU ld's offset_adjust).
        const auto moved = [&](std::uint64_t value) {
            std::size_t index = 0;
            while (index + 1 < entries.size() && entries[index + 1].offset <= value) ++index;
            if (entries.empty()) return value;
            const auto& entry = entries[index];
            if (!entry.removed) return value + entry.new_offset - entry.offset;
            std::uint64_t next = section->size;
            for (auto other = index + 1; other < entries.size(); ++other) {
                if (!entries[other].removed) {
                    next = entries[other].new_offset;
                    break;
                }
            }
            return value + next - entry.offset;
        };
        auto& file = *section->file;
        for (auto& symbol : file.symbols) {
            if (symbol.shndx < file.sections.size() && file.sections[symbol.shndx].get() == section &&
                symbol.type != elf::stt_section) {
                if (symbol.global && symbol.global->section == section) symbol.global->value = moved(symbol.value);
                symbol.value = moved(symbol.value);
            }
        }
        section->data = std::move(data);
        section->relocations = std::move(relocations);
    }
    if (frame_header_) {
        std::uint64_t count = 0;
        for (auto* section : sections) {
            for (const auto& entry : frames_[section].entries) {
                if (!entry.removed && entry.kind == FrameEntry::Kind::Fde) ++count;
            }
        }
        frame_header_->size = present ? 8 + (frame_table_ ? 4 + 8 * count : 0) : 0;
        frame_header_->data.assign(frame_header_->size, 0);
    }
}

void Link::create_frame_header() {
    // GNU ld adds .eh_frame_hdr to the first input with an .eh_frame of more
    // than a terminator, with a hidden __GNU_EH_FRAME_HDR at its start.
    for (const auto& file : files_) {
        const auto found = std::find_if(file->sections.begin(), file->sections.end(), [](const auto& section) {
            return section && section->included() && section->name == ".eh_frame" && section->size > 8;
        });
        if (found == file->sections.end()) continue;
        auto header = std::make_unique<InputSection>();
        header->file = file.get();
        header->name = ".eh_frame_hdr";
        header->flags = elf::shf_alloc;
        header->alignment = 4;
        header->keep = true;
        frame_header_ = header.get();
        file->sections.push_back(std::move(header));
        auto& symbol = this->symbol("__GNU_EH_FRAME_HDR");
        if (!symbol.defined()) {
            symbol.kind = Symbol::Kind::Defined;
            symbol.section = frame_header_;
            symbol.value = 0;
            symbol.hidden = true;
            symbol.file = file.get();
        }
        return;
    }
}

void Link::patch_frame_pointers(OutputSection& output) {
    // FDEs whose CIE merged into another input section point at it.
    for (auto* section : output.sections) {
        const auto found = frames_.find(section);
        if (found == frames_.end() || !section->included()) continue;
        for (const auto& entry : found->second.entries) {
            if (entry.removed || entry.kind != FrameEntry::Kind::Fde || entry.kept_section == section) continue;
            const auto value = (section->offset + entry.new_offset + 4) - (entry.kept_section->offset + entry.kept_offset);
            const auto at = section->offset + entry.new_offset + 4;
            for (unsigned index = 0; index < 4; ++index) {
                const unsigned shift = object_.big_endian ? (3 - index) * 8 : index * 8;
                output.data[at + index] = static_cast<std::uint8_t>(value >> shift);
            }
        }
    }
}

void Link::write_frame_header(OutputSection& frames) {
    // GNU ld's .eh_frame_hdr: version 1, a PC-relative pointer to .eh_frame,
    // and (when every FDE could be read) the FDE count and a table of
    // (initial location, FDE) pairs relative to the header, sorted.
    if (!frame_header_ || !frame_header_->included() || !frame_header_->output || frame_header_->size == 0) return;
    auto& output = *frame_header_->output;
    if (output.data.size() < frame_header_->offset + frame_header_->size) return;
    const unsigned pointer = object_.elf64 ? 8 : 4;
    const auto read = [&](std::uint64_t at, unsigned width, bool is_signed) {
        std::uint64_t value = 0;
        for (unsigned index = 0; index < width; ++index) {
            const unsigned shift = object_.big_endian ? (width - 1 - index) * 8 : index * 8;
            value |= std::uint64_t{frames.data[at + index]} << shift;
        }
        if (is_signed && width < 8 && (value >> (width * 8 - 1)) != 0) value |= ~std::uint64_t{0} << (width * 8);
        return value;
    };
    struct Row {
        std::uint64_t location;
        std::uint64_t range;
        std::uint64_t fde;
    };
    std::vector<Row> rows;
    bool table = frame_table_;
    for (auto* section : frames.sections) {
        const auto found = frames_.find(section);
        if (found == frames_.end() || !section->included()) continue;
        for (const auto& entry : found->second.entries) {
            if (entry.removed || entry.kind != FrameEntry::Kind::Fde) continue;
            const auto width = encoding_width(entry.fde_encoding, pointer);
            const auto field = section->offset + entry.new_offset + 8;
            const bool is_signed = (entry.fde_encoding & pe_signed) != 0;
            auto location = read(field, width, is_signed);
            if (location != 0) {
                switch (entry.fde_encoding & 0x70) {
                case pe_absptr: break;
                case pe_pcrel: location += frames.address + field; break;
                default: table = false; break;
                }
            }
            if (pointer == 4) location &= 0xffffffffU;
            rows.push_back({location, read(field + width, width, false), frames.address + section->offset + entry.new_offset});
        }
    }
    const auto base = output.address + frame_header_->offset;
    auto* bytes = output.data.data() + frame_header_->offset;
    const auto put32 = [&](std::uint64_t at, std::uint64_t value) {
        for (unsigned index = 0; index < 4; ++index) {
            const unsigned shift = object_.big_endian ? (3 - index) * 8 : index * 8;
            bytes[at + index] = static_cast<std::uint8_t>(value >> shift);
        }
    };
    bytes[0] = 1;
    bytes[1] = pe_pcrel | pe_sdata4;
    put32(4, frames.address - (base + 4));
    if (!table || rows.empty() || frame_header_->size < 12 + 8 * rows.size()) {
        bytes[2] = pe_omit;
        bytes[3] = pe_omit;
        return;
    }
    bytes[2] = pe_udata4;
    bytes[3] = pe_datarel | pe_sdata4;
    put32(8, rows.size());
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        return a.location != b.location ? a.location < b.location : a.range < b.range;
    });
    bool overlap = false;
    bool overflow = false;
    for (std::size_t index = 0; index < rows.size(); ++index) {
        const auto location = rows[index].location - base;
        const auto fde = rows[index].fde - base;
        const auto fits = [](std::uint64_t value) {
            const auto low = static_cast<std::int64_t>(static_cast<std::int32_t>(static_cast<std::uint32_t>(value)));
            return static_cast<std::uint64_t>(low) == value;
        };
        if (object_.elf64 && (!fits(location) || !fits(fde))) overflow = true;
        put32(12 + 8 * index, location);
        put32(16 + 8 * index, fde);
        if (index != 0 && rows[index].location < rows[index - 1].location + rows[index - 1].range) overlap = true;
    }
    if (overflow) diagnostics_.command_error(".eh_frame_hdr entry overflow");
    if (overlap) diagnostics_.command_error(".eh_frame_hdr refers to overlapping FDEs");
}

} // namespace cross::linker
