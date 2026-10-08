// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/initialization.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace cross::mir {
namespace {
bool covers(std::span<const InitializedBitRange> assigned,
            std::uint64_t begin, std::uint64_t end) {
    return std::any_of(assigned.begin(), assigned.end(),
        [&](const auto& available) { return available.begin <= begin && available.end >= end; });
}
} // namespace

bool initialized_type(const hir::Module& module, hir::TypeId id,
                      std::uint64_t base_bits,
                      std::span<const InitializedBitRange> assigned,
                      const TypeStorageSize& storage_size) {
    struct Frame {
        hir::TypeId type;
        std::uint64_t base_bits{};
        std::size_t cursor{};
        std::uint64_t element_bits{};
        bool started{}, any_member{};
    };
    std::vector<Frame> frames{{id, base_bits}};
    std::optional<bool> completed;
    const auto finish = [&](bool complete) {
        frames.pop_back();
        completed = complete;
    };
    while (!frames.empty()) {
        auto& frame = frames.back();
        const auto& type = module.type(frame.type);
        if (!frame.started) {
            frame.started = true;
            // A whole-storage write is a sufficient fast proof at any size.
            // Otherwise visit semantic fields; padding is not an output field.
            const auto bits = storage_size(frame.type) * 8;
            if (covers(assigned, frame.base_bits, frame.base_bits + bits)) {
                finish(true);
                continue;
            }
            if (type.kind == hir::Type::Kind::Array && type.element)
                frame.element_bits = storage_size(*type.element) * 8;
        }
        if (type.kind == hir::Type::Kind::Record && type.record) {
            const auto& record = module.record(*type.record);
            if (completed) {
                const bool complete = *completed;
                completed.reset();
                if ((!record.is_union && !complete) || (record.is_union && complete)) {
                    finish(complete);
                    continue;
                }
            }
            while (frame.cursor < record.members.size() &&
                   record.members[frame.cursor].name.empty() &&
                   record.members[frame.cursor].bit_width) ++frame.cursor;
            if (frame.cursor == record.members.size()) {
                finish(!record.is_union || !frame.any_member);
                continue;
            }
            frame.any_member = true;
            const auto& member = record.members[frame.cursor++];
            const auto member_base = frame.base_bits + member.offset * 8;
            if (member.bit_width) {
                completed = covers(assigned, member_base + member.bit_offset,
                                   member_base + member.bit_offset + *member.bit_width);
            } else frames.push_back({member.type, member_base});
            continue;
        }
        if (type.kind == hir::Type::Kind::Array && type.element) {
            if (completed) {
                const bool complete = *completed;
                completed.reset();
                if (!complete) { finish(false); continue; }
            }
            if (frame.cursor == type.lanes || frame.element_bits == 0) {
                finish(true);
                continue;
            }
            const auto element_base = frame.base_bits + frame.element_bits * frame.cursor++;
            frames.push_back({*type.element, element_base});
            continue;
        }
        // The initial whole-storage proof already failed for this scalar leaf.
        finish(false);
    }
    return completed.value_or(false);
}

} // namespace cross::mir
