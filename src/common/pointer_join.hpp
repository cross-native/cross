// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace cross {

// A target supplies the unique implicit destination, not a pointer-width guess.
using AddressSpaceJoin = std::function<std::optional<std::uint32_t>(std::uint32_t, std::uint32_t)>;
enum class PointerJoinKind { Other, Void, Function, Pointer, Array, Vector };
enum class PointerJoinEquality { Different, Same, Deferred };

template <class Type>
struct PointerJoinNode {
    PointerJoinKind kind{PointerJoinKind::Other};
    std::optional<Type> child;
    bool is_const{}, is_volatile{}, is_atomic{}, is_restrict{};
    std::uint32_t address_space{}, extent{};
    bool scalable{}, deferred_extent{};
};

template <class Type>
struct PointerJoinResult {
    std::optional<Type> type;
    bool deferred{};
    bool qualification_changed{};
};

// Traits expose typed structure and rebuild immutable snapshots. The algorithm
// is shared by source types and HIR IDs; leaf equality owns nominal/callable
// identity. Only the outer pointer may change address space or bridge void.
template <class Traits>
PointerJoinResult<typename Traits::Type> join_pointer_pointees(
    Traits& traits, typename Traits::Type left, typename Traits::Type right,
    unsigned depth = 0) {
    using TypeRef = typename Traits::Type;
    using Result = PointerJoinResult<TypeRef>;
    struct Frame {
        TypeRef base;
        PointerJoinNode<TypeRef> a, b, result;
        bool deferred{};
    };
    // Follow the single structural child iteratively, then rebuild inside out.
    // No unrelated hardcoded nesting limit may change language compatibility.
    std::vector<Frame> frames;
    bool immediate = depth == 0;
    const auto finish = [&](Frame& frame, bool changed) {
        auto& node = frame.result;
        node.is_const = node.is_const || frame.a.is_const || frame.b.is_const;
        node.is_volatile = frame.a.is_volatile || frame.b.is_volatile;
        node.is_restrict = frame.a.is_restrict && frame.b.is_restrict;
        changed = changed || node.is_const != frame.a.is_const || node.is_const != frame.b.is_const ||
            node.is_volatile != frame.a.is_volatile || node.is_volatile != frame.b.is_volatile;
        return Result{traits.rebuild(frame.base, node), frame.deferred, changed};
    };
    Result joined;
    for (;;) {
        auto a = traits.describe(left), b = traits.describe(right);
        if (a.is_atomic != b.is_atomic) return {};
        Frame frame{left, a, b, a};
        if (immediate && (a.kind == PointerJoinKind::Void || b.kind == PointerJoinKind::Void)) {
            if (a.kind == PointerJoinKind::Function || b.kind == PointerJoinKind::Function) return {};
            if (b.kind == PointerJoinKind::Void) { frame.base = right; frame.result = b; }
        } else {
            if (a.kind != b.kind) return {};
            if (a.kind == PointerJoinKind::Pointer || a.kind == PointerJoinKind::Array ||
                a.kind == PointerJoinKind::Vector) {
                if (!a.child || !b.child) return {};
                if (a.kind == PointerJoinKind::Pointer) {
                    if (a.address_space != b.address_space) return {};
                } else {
                    frame.deferred = a.deferred_extent || b.deferred_extent;
                    if (a.scalable != b.scalable || (!frame.deferred && a.extent != b.extent) ||
                        (frame.deferred && ((!a.deferred_extent && !a.extent) ||
                                           (!b.deferred_extent && !b.extent)))) return {};
                    // Keep the actual unresolved requirement, never a guessed size.
                    if (!a.deferred_extent && b.deferred_extent) { frame.base = right; frame.result = b; }
                }
                frames.push_back(std::move(frame));
                left = *a.child;
                right = *b.child;
                immediate = false;
                continue;
            }
            const auto equal = traits.equal_leaf(left, right);
            if (equal == PointerJoinEquality::Different) return {};
            frame.deferred = equal == PointerJoinEquality::Deferred;
        }
        joined = finish(frame, false);
        break;
    }
    while (!frames.empty()) {
        auto frame = std::move(frames.back());
        frames.pop_back();
        frame.result.child = *joined.type;
        frame.deferred = frame.deferred || joined.deferred;
        // A deeper qualification addition requires every intervening
        // destination pointer to be const (arrays are not pointer layers).
        if (frame.a.kind == PointerJoinKind::Pointer && joined.qualification_changed)
            frame.result.is_const = true;
        joined = finish(frame, joined.qualification_changed);
    }
    return joined;
}

template <class Traits>
PointerJoinResult<typename Traits::Type> join_pointer_types(
    Traits& traits, typename Traits::Type left, typename Traits::Type right,
    const AddressSpaceJoin& spaces = {}) {
    auto a = traits.describe(left), b = traits.describe(right);
    if (a.kind != PointerJoinKind::Pointer || b.kind != PointerJoinKind::Pointer || !a.child || !b.child)
        return {};
    const auto space = a.address_space == b.address_space ? std::optional{a.address_space}
        : spaces ? spaces(a.address_space, b.address_space) : std::nullopt;
    if (!space) return {};
    auto result = join_pointer_pointees(traits, *a.child, *b.child);
    if (!result.type) return {};
    a.child = *result.type;
    a.address_space = *space;
    a.is_const = a.is_volatile = a.is_atomic = a.is_restrict = false;
    result.type = traits.rebuild(left, a);
    result.qualification_changed = false;
    return result;
}

} // namespace cross
