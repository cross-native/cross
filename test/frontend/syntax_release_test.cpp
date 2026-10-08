// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <iostream>
#include <new>
#include <source_location>

namespace {
bool forbid_allocation{};
struct NoAllocation {
    NoAllocation() { forbid_allocation = true; }
    ~NoAllocation() { forbid_allocation = false; }
};
void require(bool condition,
             std::source_location at = std::source_location::current()) {
    if (condition) return;
    std::cerr << at.line() << ": syntax ownership check failed\n";
    std::abort();
}
template<class Value>
void destroy_and_poison(Value* value) noexcept {
    value->~Value();
    // Volatile stores keep the ownership negative control effective even when
    // the host optimizer removes ordinary memset-before-delete dead stores.
    auto* bytes = static_cast<volatile unsigned char*>(static_cast<void*>(value));
    for (std::size_t index = 0; index < sizeof(Value); ++index) bytes[index] = 0xA5;
    ::operator delete(value);
}
// Out of line so GCC does not pair an inlined free() with an operator new call.
[[gnu::noinline]] void release(void* pointer) noexcept { std::free(pointer); }
} // namespace

void* operator new(std::size_t bytes) {
    if (forbid_allocation) std::abort();
    if (auto* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { release(pointer); }
void operator delete[](void* pointer) noexcept { release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { release(pointer); }

int main() {
    using namespace cross;
    constexpr unsigned depth = 50000;
    // Direct children and token-splice edges share the same normal release.
    for (bool splice : {false, true}) {
        std::shared_ptr<const SyntaxNode> root;
        std::weak_ptr<const SyntaxNode> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<SyntaxNode>();
            if (!index) leaf = next;
            if (root) {
                if (splice) {
                    next->tokens.emplace_back();
                    next->tokens.back().kind = TokenKind::StructuredSplice;
                    next->tokens.back().splice = std::move(root);
                } else next->children.push_back(std::move(root));
            }
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    {
        std::shared_ptr<const SyntaxMatchValue> root;
        std::weak_ptr<const SyntaxMatchValue> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<SyntaxMatchValue>();
            if (!index) leaf = next;
            if (root) {
                next->fields.emplace_back();
                next->fields.back().records.push_back(std::move(root));
            }
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    // Alternate both classes and all retained parsed/raw token paths. Running
    // two independent drains recursively would not suffice for this graph.
    for (unsigned path = 0; path < 3; ++path) {
        std::shared_ptr<const SyntaxNode> root;
        std::weak_ptr<const SyntaxNode> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<SyntaxNode>();
            if (!index) leaf = next;
            if (root) {
                auto match = std::make_shared<SyntaxMatchValue>();
                if (path == 0) {
                    match->fields.emplace_back();
                    match->fields.back().node = std::move(root);
                } else if (path == 1) {
                    match->input.emplace_back();
                    match->input.back().splice = std::move(root);
                } else {
                    match->fields.emplace_back();
                    match->fields.back().tokens.emplace_back();
                    match->fields.back().tokens.back().splice = std::move(root);
                }
                next->match = std::move(match);
            }
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    {
        auto child = std::make_shared<SyntaxNode>();
        child->production = SyntaxProduction::AssignmentExpression;
        child->context = std::make_shared<SyntaxContext>();
        child->children.push_back(std::make_shared<SyntaxNode>());
        auto match = std::make_shared<SyntaxMatchValue>();
        match->fields.emplace_back();
        match->fields.back().node = child;
        match->input.emplace_back();
        match->input.back().splice = child;
        auto root = std::make_shared<SyntaxNode>();
        root->children = {child, child};
        root->match = std::move(match);
        const auto context = child->context;
        const auto leaf = child->children.front();
        { NoAllocation guard; root.reset(); }
        require(child->production == SyntaxProduction::AssignmentExpression &&
                child->context == context && child->children.size() == 1 &&
                child->children.front() == leaf);
    }
    {
        auto queued = std::make_shared<SyntaxNode>();
        queued->context = std::make_shared<SyntaxContext>();
        queued->children.push_back(std::make_shared<SyntaxNode>());
        queued->match = std::make_shared<SyntaxMatchValue>();
        const auto context = queued->context;
        const auto leaf = queued->children.front();
        const auto match = queued->match;
        std::weak_ptr<const SyntaxNode> weak = queued;
        std::shared_ptr<const SyntaxNode> retained;
        std::shared_ptr<SyntaxNode> copied;
        auto trigger = std::shared_ptr<const SyntaxNode>(new SyntaxNode,
            [&](const SyntaxNode* node) {
                retained = weak.lock();
                require(retained && retained->children.size() == 1);
                copied = std::make_shared<SyntaxNode>(*retained);
                SyntaxNode moved(std::move(*copied));
                *copied = moved;
                SyntaxNode assigned;
                assigned = *retained;
                *copied = std::move(assigned);
                delete node;
            });
        auto root = std::make_shared<SyntaxNode>();
        root->children.push_back(std::move(queued));
        root->children.push_back(std::move(trigger));
        root.reset();
        require(retained && retained->context == context && retained->match == match &&
                retained->children.size() == 1 && retained->children.front() == leaf);
        require(copied && copied->context == context && copied->match == match &&
                copied->children.size() == 1 && copied->children.front() == leaf);
        { NoAllocation guard; retained.reset(); copied.reset(); }
        require(weak.expired());
    }
    {
        auto queued = std::make_shared<SyntaxMatchValue>();
        queued->context = std::make_shared<SyntaxContext>();
        queued->fields.emplace_back();
        queued->fields.back().node = std::make_shared<SyntaxNode>();
        queued->fields.back().records.push_back(std::make_shared<SyntaxMatchValue>());
        const auto context = queued->context;
        const auto node = queued->fields.back().node;
        const auto record = queued->fields.back().records.front();
        std::weak_ptr<const SyntaxMatchValue> weak = queued;
        std::shared_ptr<const SyntaxMatchValue> retained;
        std::shared_ptr<SyntaxMatchValue> copied;
        auto trigger = std::shared_ptr<const SyntaxMatchValue>(new SyntaxMatchValue,
            [&](const SyntaxMatchValue* value) {
                retained = weak.lock();
                require(retained && retained->fields.size() == 1);
                copied = std::make_shared<SyntaxMatchValue>(*retained);
                SyntaxMatchValue moved(std::move(*copied));
                *copied = moved;
                SyntaxMatchValue assigned;
                assigned = *retained;
                *copied = std::move(assigned);
                delete value;
            });
        auto root = std::make_shared<SyntaxMatchValue>();
        root->fields.emplace_back();
        root->fields.back().records.push_back(std::move(queued));
        root->fields.back().records.push_back(std::move(trigger));
        root.reset();
        for (const auto* value : {retained.get(), static_cast<const SyntaxMatchValue*>(copied.get())})
            require(value && value->context == context && value->fields.size() == 1 &&
                    value->fields.front().node == node &&
                    value->fields.front().records.size() == 1 &&
                    value->fields.front().records.front() == record);
        { NoAllocation guard; retained.reset(); copied.reset(); }
        require(weak.expired());
    }
    // An aliasing handle queues the pointed-to child's private link, but its
    // actual control block destroys the parent. Never manufacture ownership.
    {
        auto parent = std::make_shared<SyntaxNode>();
        parent->children.push_back(std::make_shared<SyntaxNode>());
        std::weak_ptr<const SyntaxNode> weak_parent = parent;
        std::weak_ptr<const SyntaxNode> weak_child = parent->children.front();
        std::shared_ptr<const SyntaxNode> alias(parent, parent->children.front().get());
        auto root = std::make_shared<SyntaxNode>();
        root->children.push_back(std::move(alias));
        parent.reset();
        { NoAllocation guard; root.reset(); }
        require(weak_parent.expired() && weak_child.expired());
    }
    {
        auto parent = std::make_shared<SyntaxNode>();
        parent->match = std::make_shared<SyntaxMatchValue>();
        std::weak_ptr<const SyntaxNode> weak_parent = parent;
        std::weak_ptr<const SyntaxMatchValue> weak_match = parent->match;
        std::shared_ptr<const SyntaxMatchValue> alias(parent, parent->match.get());
        auto root = std::make_shared<SyntaxMatchValue>();
        root->fields.emplace_back();
        root->fields.back().records.push_back(std::move(alias));
        parent.reset();
        { NoAllocation guard; root.reset(); }
        require(weak_parent.expired() && weak_match.expired());
    }
    {
        // Two different parents own the same child, and both aliasing handles
        // point at that child. Poison released storage so a duplicate queue
        // entry cannot accidentally survive by reading unchanged freed bytes.
        auto child = std::shared_ptr<SyntaxNode>(new SyntaxNode, destroy_and_poison<SyntaxNode>);
        std::weak_ptr<const SyntaxNode> weak = child;
        auto first = std::make_shared<SyntaxNode>();
        auto second = std::make_shared<SyntaxNode>();
        first->children.push_back(child);
        second->children.push_back(child);
        auto root = std::make_shared<SyntaxNode>();
        root->children.emplace_back(first, child.get());
        root->children.emplace_back(second, child.get());
        child.reset(); first.reset(); second.reset();
        { NoAllocation guard; root.reset(); }
        require(weak.expired());
    }
    {
        auto child = std::shared_ptr<SyntaxMatchValue>(new SyntaxMatchValue,
            destroy_and_poison<SyntaxMatchValue>);
        std::weak_ptr<const SyntaxMatchValue> weak = child;
        auto first = std::make_shared<SyntaxNode>();
        auto second = std::make_shared<SyntaxNode>();
        first->match = child;
        second->match = child;
        auto root = std::make_shared<SyntaxMatchValue>();
        root->fields.emplace_back();
        root->fields.back().records.emplace_back(first, child.get());
        root->fields.back().records.emplace_back(second, child.get());
        child.reset(); first.reset(); second.reset();
        { NoAllocation guard; root.reset(); }
        require(weak.expired());
    }
    // Retained public-tree context metadata reaches the typed AST drain too;
    // the same aliasing contract applies to its type and expression handles.
    {
        TypePtr child(new Type, destroy_and_poison<Type>);
        std::weak_ptr<Type> weak = child;
        auto first = std::make_shared<Type>();
        auto second = std::make_shared<Type>();
        first->pointee = child;
        second->pointee = child;
        auto root = std::make_shared<Type>();
        root->pointee = TypePtr(first, child.get());
        root->element = TypePtr(second, child.get());
        child.reset(); first.reset(); second.reset();
        { NoAllocation guard; root.reset(); }
        require(weak.expired());
    }
    {
        auto child = std::shared_ptr<const Expr>(new Expr, destroy_and_poison<Expr>);
        std::weak_ptr<const Expr> weak = child;
        auto first = std::make_shared<Type>();
        auto second = std::make_shared<Type>();
        first->array_bound = child;
        second->array_bound = child;
        auto root = std::make_shared<Type>();
        root->array_bound = std::shared_ptr<const Expr>(first, child.get());
        root->vector_bound = std::shared_ptr<const Expr>(second, child.get());
        child.reset(); first.reset(); second.reset();
        { NoAllocation guard; root.reset(); }
        require(weak.expired());
    }
    std::cout << "six deep syntax ownership paths and shared/weak/alias controls passed\n";
}
