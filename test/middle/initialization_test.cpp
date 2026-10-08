// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/initialization.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <vector>

int main() {
    using namespace cross;
    using mir::InitializedBitRange;
    const auto require = [](bool ok, const std::source_location at = std::source_location::current()) {
        if (!ok) {
            std::cerr << at.file_name() << ':' << at.line() << ": initialization check failed\n";
            std::abort();
        }
    };
    for (const unsigned address_bits : {32U, 64U}) {
        hir::Module module;
        module.address_bits = address_bits;
        const std::uint64_t word = address_bits / 8;
        std::vector<std::uint64_t> sizes;
        const auto add_type = [&](hir::Type type, std::uint64_t size) {
            const hir::TypeId id{static_cast<std::uint32_t>(module.types.size())};
            module.types.push_back(std::move(type));
            sizes.push_back(size);
            return id;
        };
        hir::Type byte_type;
        byte_type.builtin = BuiltinType::U8;
        const auto byte = add_type(byte_type, 1);
        hir::Type word_type;
        word_type.builtin = BuiltinType::Uptr;
        const auto integer = add_type(word_type, word);
        const auto member = [](const char* name, hir::TypeId type, std::uint64_t offset) {
            hir::RecordMember value;
            value.name = name;
            value.type = type;
            value.offset = offset;
            return value;
        };
        const auto add_record = [&](std::vector<hir::RecordMember> members,
                                    std::uint64_t size, bool is_union = false) {
            hir::Record record;
            record.id = {static_cast<std::uint32_t>(module.records.size())};
            record.complete = true;
            record.is_union = is_union;
            record.size = size;
            record.members = std::move(members);
            hir::Type type;
            type.kind = hir::Type::Kind::Record;
            type.record = record.id;
            module.records.push_back(std::move(record));
            return add_type(type, size);
        };
        const auto add_array = [&](hir::TypeId element, std::uint32_t lanes) {
            hir::Type type;
            type.kind = hir::Type::Kind::Array;
            type.element = element;
            type.lanes = lanes;
            return add_type(type, sizes[element.value] * lanes);
        };
        const auto size = [&](hir::TypeId type) { return sizes.at(type.value); };
        const auto initialized = [&](hir::TypeId type, const std::vector<InitializedBitRange>& ranges,
                                      std::uint64_t base = 0) {
            return mir::initialized_type(module, type, base, ranges, size);
        };
        const auto leaf = add_record({member("tag", byte, 0), member("value", integer, word)}, 2 * word);
        const std::vector<InitializedBitRange> fields{{0, 8}, {word * 8, word * 16}};
        auto nested = leaf;
        for (unsigned depth = 0; depth <= 240; ++depth) {
            require(initialized(nested, fields));
            require(!initialized(nested, {{0, 8}}));
            require(initialized(nested, {{0, word * 16}}));
            require(initialized(nested, {{104, 112}, {104 + word * 8, 104 + word * 16}}, 104));
            nested = add_record({member("child", add_array(nested, 1), 0)}, 2 * word);
        }
        const auto siblings = add_record({member("left", nested, 0), member("right", nested, 2 * word)}, 4 * word);
        require(!initialized(siblings, fields));
        require(initialized(siblings, {{0, 8}, {word * 8, word * 16 + 8}, {word * 24, word * 32}}));
        const auto choice = add_record({member("selected", nested, 0), member("alternate", integer, 0)}, 2 * word, true);
        require(initialized(choice, fields));
        require(initialized(choice, {{0, word * 8}}));
        require(!initialized(choice, {{0, 8}}));

        // Large arrays still require semantic fields, not padding. Merge the
        // adjacent value/tag intervals just as source-MIR dataflow does.
        const auto large = add_array(leaf, 4097);
        std::vector<InitializedBitRange> large_fields;
        const auto append = [&](InitializedBitRange range) {
            if (!large_fields.empty() && large_fields.back().end == range.begin)
                large_fields.back().end = range.end;
            else large_fields.push_back(range);
        };
        for (std::uint64_t index = 0; index < 4097; ++index) {
            const auto base = index * word * 16;
            append({base, base + 8});
            append({base + word * 8, base + word * 16});
        }
        require(initialized(large, large_fields));
        large_fields.pop_back();
        require(!initialized(large, large_fields));
        require(!initialized(add_array(leaf, 0xffffffffU), {}));
        const auto empty = add_record({}, 0);
        require(initialized(add_array(empty, 0xffffffffU), {}));

        // HIR owns endian-dependent bit placement; the visitor consumes either
        // placement without rounding named fields to their carrier/padding bits.
        for (const bool high_bits : {false, true}) {
            auto first = member("first", integer, 0);
            first.bit_width = 3;
            first.bit_offset = high_bits ? 29 : 0;
            auto second = member("second", integer, 0);
            second.bit_width = 5;
            second.bit_offset = high_bits ? 16 : 8;
            auto unnamed = member("", integer, 0);
            unnamed.bit_width = 7;
            const auto bits = add_record({first, second, unnamed, member("value", integer, word)}, 2 * word);
            const auto a = InitializedBitRange{first.bit_offset, first.bit_offset + *first.bit_width};
            const auto b = InitializedBitRange{second.bit_offset, second.bit_offset + *second.bit_width};
            std::vector<InitializedBitRange> assigned = high_bits
                ? std::vector<InitializedBitRange>{b, a, {word * 8, word * 16}}
                : std::vector<InitializedBitRange>{a, b, {word * 8, word * 16}};
            require(initialized(bits, assigned));
            require(!initialized(bits, {a, {word * 8, word * 16}}));
            require(initialized(add_record({member("bits", bits, 0), member("empty", empty, 0)}, 2 * word), assigned));
        }
    }
}
