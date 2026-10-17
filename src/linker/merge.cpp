// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/link.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <tuple>
#include <unordered_map>

namespace cross::linker {
namespace {

struct Entry {
    std::string_view bytes;
    // The alignment the entry needs; zero once it is a suffix of another.
    std::uint64_t alignment{};
    const Entry* suffix_of{};
    std::uint64_t output{};
};

// Orders strings by their reversed bytes without the terminator, so that a
// string comes just before the strings it is a suffix of; with one common
// alignment above the character size, by the tail that alignment leaves
// first (GNU ld's strrevcmp and strrevcmp_align).
bool reverse_less(const Entry* a, const Entry* b, std::uint64_t terminator,
                  std::uint64_t common_alignment) {
    const auto length_a = a->bytes.size() - terminator;
    const auto length_b = b->bytes.size() - terminator;
    if (common_alignment != 0) {
        const auto tail_a = static_cast<std::int64_t>(length_a & (common_alignment - 1));
        const auto tail_b = static_cast<std::int64_t>(length_b & (common_alignment - 1));
        if (tail_a != tail_b) return tail_a < tail_b;
    }
    for (std::uint64_t index = 1; index <= std::min(length_a, length_b); ++index) {
        const auto ca = static_cast<unsigned char>(a->bytes[length_a - index]);
        const auto cb = static_cast<unsigned char>(b->bytes[length_b - index]);
        if (ca != cb) return ca < cb;
    }
    return length_a < length_b;
}

// The length of the string at `at`, its terminator included; a string the
// section does not terminate takes an implicit one.
std::uint64_t string_length(const std::vector<std::uint8_t>& data, std::uint64_t at,
                            std::uint64_t width) {
    for (std::uint64_t end = at; end < data.size(); end += width) {
        bool zero = true;
        for (std::uint64_t index = 0; index < width && end + index < data.size(); ++index) {
            if (data[end + index] != 0) zero = false;
        }
        if (zero) return end + width - at;
    }
    return data.size() - at + width;
}

} // namespace

std::uint64_t Link::section_address(const InputSection& section, std::uint64_t offset) const {
    if (!section.merged) return mask(address(section) + offset);
    const auto& pieces = section.pieces;
    auto next = std::upper_bound(pieces.begin(), pieces.end(), offset,
                                 [](std::uint64_t value, const auto& piece) { return value < piece.first; });
    if (next == pieces.begin()) return mask(address(*section.merged) + offset);
    const auto& piece = *std::prev(next);
    return mask(address(*section.merged) + piece.second + (offset - piece.first));
}

void Link::merge_sections() {
    // GNU ld's merging: relocation-free SHF_MERGE sections of one output
    // section, flag set, entry size, and alignment share one copy of each
    // entry, in the order the entries first appear; strings also share the
    // tails of longer strings. The group's first section in file order holds
    // the result.
    using Key = std::tuple<OutputSection*, std::uint64_t, std::uint64_t, std::uint64_t>;
    std::unordered_map<const InputSection*, OutputSection*> outputs;
    for (const auto& placement : placements_) {
        for (const auto& item : placement->items) {
            for (const auto* section : item.sections) outputs[section] = placement->output;
        }
    }
    std::vector<std::pair<Key, std::vector<InputSection*>>> groups;
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included() || (section->flags & elf::shf_merge) == 0 ||
                section->nobits() || !outputs[section.get()]) {
                continue;
            }
            const auto width = section->entry_size;
            const bool strings = (section->flags & elf::shf_strings) != 0;
            const auto alignment = section->alignment;
            if (section->size == 0 || width == 0 || section->size % width != 0 ||
                !section->relocations.empty() ||
                (width < alignment && ((width & (width - 1)) != 0 || !strings)) ||
                (width > alignment && (width & (alignment - 1)) != 0)) {
                continue;
            }
            const Key key{outputs[section.get()], section->flags & (elf::shf_merge | elf::shf_strings),
                          width, alignment};
            auto group = std::find_if(groups.begin(), groups.end(),
                                      [&](const auto& entry) { return entry.first == key; });
            if (group == groups.end()) {
                groups.emplace_back(key, std::vector<InputSection*>{});
                group = std::prev(groups.end());
            }
            group->second.push_back(section.get());
        }
    }
    for (auto& [key, sections] : groups) {
        const auto width = std::get<2>(key);
        const auto section_alignment = std::get<3>(key);
        const bool strings = (std::get<1>(key) & elf::shf_strings) != 0;
        // Entries by contents, in first-appearance order; each input piece
        // refers to its entry.
        std::vector<std::unique_ptr<Entry>> entries;
        std::unordered_map<std::string_view, Entry*> index;
        std::vector<std::vector<std::pair<std::uint64_t, Entry*>>> pieces(sections.size());
        std::vector<std::vector<std::uint8_t>> padded(sections.size());
        bool padded_sizes = true;
        for (std::size_t member = 0; member < sections.size(); ++member) {
            auto& data = padded[member];
            data = sections[member]->data;
            if (strings) data.resize(data.size() + width, 0);
            const auto size = sections[member]->size;
            if (size % section_alignment != 0) padded_sizes = false;
            for (std::uint64_t at = 0; at < size;) {
                const auto length = strings ? std::min<std::uint64_t>(string_length(sections[member]->data, at, width),
                                                                      data.size() - at)
                                            : width;
                const std::string_view bytes(reinterpret_cast<const char*>(data.data() + at), length);
                std::uint64_t alignment = at & (~at + 1);
                if (alignment == 0 || alignment > section_alignment) alignment = section_alignment;
                auto& entry = index[bytes];
                if (!entry) {
                    entries.push_back(std::make_unique<Entry>());
                    entry = entries.back().get();
                    entry->bytes = bytes;
                }
                entry->alignment = std::max(entry->alignment, alignment);
                pieces[member].emplace_back(at, entry);
                at += length;
            }
        }
        if (strings) {
            std::uint64_t common = 0;
            for (const auto& entry : entries) {
                if (common == 0) {
                    common = entry->alignment;
                } else if (common != entry->alignment) {
                    common = ~std::uint64_t{0};
                }
            }
            const std::uint64_t sort_alignment =
                common != ~std::uint64_t{0} && common > width ? common : 0;
            std::vector<Entry*> sorted;
            for (const auto& entry : entries) sorted.push_back(entry.get());
            std::sort(sorted.begin(), sorted.end(), [&](const Entry* a, const Entry* b) {
                return reverse_less(a, b, width, sort_alignment);
            });
            if (!sorted.empty()) {
                Entry* container = sorted.back();
                for (auto position = sorted.size() - 1; position-- > 0;) {
                    Entry* candidate = sorted[position];
                    const auto difference = container->bytes.size() - candidate->bytes.size();
                    if (container->alignment >= candidate->alignment &&
                        container->bytes.size() > candidate->bytes.size() &&
                        (difference & (candidate->alignment - 1)) == 0 &&
                        container->bytes.substr(difference) == candidate->bytes) {
                        candidate->suffix_of = container;
                        candidate->alignment = 0;
                    } else {
                        container = candidate;
                    }
                }
            }
        }
        std::vector<std::uint8_t> merged;
        for (const auto& entry : entries) {
            if (entry->alignment == 0) continue;
            const auto at = (merged.size() + entry->alignment - 1) / entry->alignment * entry->alignment;
            merged.resize(at, 0);
            entry->output = at;
            merged.insert(merged.end(), entry->bytes.begin(), entry->bytes.end());
        }
        for (const auto& entry : entries) {
            if (entry->suffix_of) {
                entry->output = entry->suffix_of->output + entry->suffix_of->bytes.size() - entry->bytes.size();
            }
        }
        if (padded_sizes) {
            merged.resize((merged.size() + section_alignment - 1) / section_alignment * section_alignment, 0);
        }
        auto* holder = sections.front();
        for (std::size_t member = 0; member < sections.size(); ++member) {
            auto* section = sections[member];
            section->merged = holder;
            section->pieces.clear();
            for (const auto& [at, entry] : pieces[member]) section->pieces.emplace_back(at, entry->output);
            if (section != holder) {
                section->data.clear();
                section->size = 0;
            }
        }
        holder->data = std::move(merged);
        holder->size = holder->data.size();
    }
}

} // namespace cross::linker
