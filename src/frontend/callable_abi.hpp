// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cross {

// Pure source-type normalization, not layout or ABI transport classification.
// Visit each occurrence in preorder: callable, result, then written parameters.
// Later children stay lazy, shared occurrences are not memoized, and the first
// unknown ABI/exception leaves earlier normalizations intact. The resolver owns
// selected-model/default/alias policy; this walk never supplies a fallback.
inline bool canonicalize_callable_abis(const TypePtr& type,
    const std::function<std::optional<std::string>(std::string_view)>& canonical_abi,
    std::string* unknown = nullptr) {
    if (!canonical_abi || !type) return true;
    if (type->kind != Type::Kind::Pointer && type->kind != Type::Kind::Array &&
        type->kind != Type::Kind::Vector && type->kind != Type::Kind::Function) return true;
    struct Frame {
        TypePtr type;
        bool result_requested{};
        std::size_t next_parameter{};
    };
    std::vector<Frame> frames{{type, false, 0}};
    while (!frames.empty()) {
        auto& frame = frames.back();
        if (!frame.type) {
            frames.pop_back();
            continue;
        }
        // These wrappers have no remaining children. Replace their frame rather
        // than retaining a native or heap continuation for a tail-only edge.
        if (frame.type->kind == Type::Kind::Pointer) {
            auto child = frame.type->pointee;
            frame.type = std::move(child);
            continue;
        }
        if (frame.type->kind == Type::Kind::Array || frame.type->kind == Type::Kind::Vector) {
            auto child = frame.type->element;
            frame.type = std::move(child);
            continue;
        }
        if (frame.type->kind != Type::Kind::Function || !frame.type->function) {
            frames.pop_back();
            continue;
        }
        if (!frame.result_requested) {
            if (canonical_abi) {
                auto resolved = canonical_abi(frame.type->function->abi);
                if (!resolved) {
                    if (unknown) *unknown = frame.type->function->abi;
                    return false;
                }
                frame.type->function->abi = std::move(*resolved);
            }
            frame.result_requested = true;
            frames.push_back({frame.type->function->result, false, 0});
            continue;
        }
        const auto& parameters = frame.type->function->parameters;
        if (frame.next_parameter < parameters.size()) {
            auto child = parameters[frame.next_parameter++].type;
            frames.push_back({std::move(child), false, 0});
            continue;
        }
        frames.pop_back();
    }
    return true;
}

} // namespace cross
