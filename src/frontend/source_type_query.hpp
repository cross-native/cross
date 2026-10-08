// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cross {

// Pure graph reachability, never execution or a physical-layout query. A local
// identity set makes shared/recursive type graphs finite; no result survives this
// query or a subsequent source-table/type mutation. Retain the original edge set
// and preorder: pointee, element, callable result, then written parameter types.
// declared_array_type is separate retained source, not callable type identity.
inline bool contains_meta_type(const TypePtr& type) {
    if (!type) return false;
    if (is_meta_type(type)) return true;
    if (!type->pointee && !type->element && !type->function) return false;
    std::unordered_set<const Type*> seen;
    std::vector<TypePtr> pending{type};
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!current) continue;
        if (is_meta_type(current)) return true;
        if (!current->pointee && !current->element && !current->function) continue;
        if (!seen.insert(current.get()).second) continue;
        if (current->function) {
            for (auto iterator = current->function->parameters.rbegin();
                 iterator != current->function->parameters.rend(); ++iterator)
                if (iterator->type) pending.push_back(iterator->type);
            if (current->function->result) pending.push_back(current->function->result);
        }
        if (current->element) pending.push_back(current->element);
        if (current->pointee) pending.push_back(current->pointee);
    }
    return false;
}

// A source classifier describes types, never executes a value or requests
// physical layout. Its child query is driven by ordered heap frames. Each
// frame retains the results of already completed child requests while its
// pure type-selection step is resumed; null is a completed unknown type.
// Without a memo, results are local to this one classification.
struct SourceTypeRequest {
    const Expr* expression;
    bool decay;
    bool operator==(const SourceTypeRequest&) const = default;
};

// Completed classifications, valid only while the caller's lexical state is
// unchanged; the caller discards it when a binding enters or leaves scope.
struct SourceTypeMemo {
    struct Hash {
        std::size_t operator()(const SourceTypeRequest& request) const noexcept {
            return std::hash<const Expr*>{}(request.expression) * 2 + request.decay;
        }
    };
    std::unordered_map<SourceTypeRequest, TypePtr, Hash> types;
};

template<class Classify, class Stopped>
TypePtr classify_source_type(const Expr& expression, bool decay,
                            const Classify& classify, const Stopped& stopped,
                            SourceTypeMemo* memo = nullptr) {
    struct Frame {
        SourceTypeRequest request;
        std::vector<TypePtr> children;
    };
    const auto remembered = [&](const SourceTypeRequest& request) -> const TypePtr* {
        if (!memo) return nullptr;
        const auto found = memo->types.find(request);
        return found == memo->types.end() ? nullptr : &found->second;
    };
    if (const auto* known = remembered({&expression, decay})) return *known;
    std::vector<Frame> frames{{{&expression, decay}, {}}};
    while (!frames.empty()) {
        if (stopped()) return {};
        auto& frame = frames.back();
        std::size_t next{};
        std::optional<SourceTypeRequest> pending;
        const auto child = [&](const Expr& source, bool child_decay = true) -> TypePtr {
            if (pending) return {};
            if (next < frame.children.size()) return frame.children[next++];
            pending = SourceTypeRequest{&source, child_decay};
            return {};
        };
        auto result = classify(*frame.request.expression, frame.request.decay, child);
        if (stopped()) return {};
        if (pending) {
            if (const auto* known = remembered(*pending)) frame.children.push_back(*known);
            else frames.push_back({*pending, {}});
            continue;
        }
        if (memo) memo->types.emplace(frame.request, result);
        frames.pop_back();
        if (frames.empty()) return result;
        frames.back().children.push_back(std::move(result));
    }
    return {};
}

} // namespace cross
