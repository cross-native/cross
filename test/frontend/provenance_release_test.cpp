// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/parser.hpp"

#include <cstdlib>
#include <iostream>
#include <new>
#include <source_location>

namespace cross {
struct ProvenanceOwnershipTestAccess {
    static auto scopes(unsigned depth) {
        std::shared_ptr<const Parser::ScopePlacement> root;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<Parser::ScopePlacement>();
            next->depth = index;
            next->parent = std::move(root);
            root = std::move(next);
        }
        return root;
    }
    static std::shared_ptr<const SyntaxContext> alias_context(const TypePtr& type) {
        auto environment = std::make_shared<SyntaxParseEnvironment>();
        environment->aliases.emplace("retained", std::make_shared<AliasDefinition>(type, 224));
        auto context = std::make_shared<SyntaxContext>();
        context->parse_environment = std::move(environment);
        return context;
    }
};
} // namespace cross

namespace {
bool forbid_allocation{};
struct NoAllocation {
    NoAllocation() { forbid_allocation = true; }
    ~NoAllocation() { forbid_allocation = false; }
};
void require(bool condition,
             std::source_location at = std::source_location::current()) {
    if (condition) return;
    std::cerr << at.line() << ": provenance ownership check failed\n";
    std::abort();
}
template<class Value>
void destroy_and_poison(Value* value) noexcept {
    value->~Value();
    auto* bytes = static_cast<volatile unsigned char*>(static_cast<void*>(value));
    for (std::size_t index = 0; index < sizeof(Value); ++index) bytes[index] = 0xA5;
    ::operator delete(value);
}
} // namespace

void* operator new(std::size_t bytes) {
    if (forbid_allocation) std::abort();
    if (auto* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

int main() {
    using namespace cross;
    constexpr unsigned depth = 50000;
    {
        std::shared_ptr<const TagBinding> root;
        std::weak_ptr<const TagBinding> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<TagBinding>();
            if (!index) leaf = next;
            next->declaration_source = std::move(root);
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    {
        std::shared_ptr<const ValuePlacementIdentity> root;
        std::weak_ptr<const ValuePlacementIdentity> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<ValuePlacementIdentity>();
            if (!index) leaf = next;
            if (root) {
                auto source = std::make_shared<ValueBinding>();
                source->placement = std::move(root);
                next->source_binding = std::move(source);
            }
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    {
        std::shared_ptr<const ValueDeclarationSource> root;
        std::weak_ptr<const ValueDeclarationSource> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<ValueDeclarationSource>();
            if (!index) leaf = next;
            next->binding.declaration_source = std::move(root);
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    {
        auto root = ProvenanceOwnershipTestAccess::scopes(depth);
        std::weak_ptr weak(root);
        { NoAllocation guard; root.reset(); }
        require(weak.expired());
    }
    {
        // Cross public-node -> context -> detached alias -> Type -> Expr ->
        // public-node ownership. It must reuse one drain, not nest host pumps.
        // This is an ownership graph, not a source-layout/constant proof.
        std::shared_ptr<const SyntaxNode> root;
        std::weak_ptr<const SyntaxNode> leaf;
        for (unsigned index = 0; index < depth; ++index) {
            auto next = std::make_shared<SyntaxNode>();
            if (!index) leaf = next;
            if (root) {
                auto bound = std::make_shared<Expr>();
                bound->kind = Expr::Kind::Quote;
                bound->quote_fragments.emplace_back();
                bound->quote_fragments.back().emplace_back();
                bound->quote_fragments.back().back().splice = std::move(root);
                auto type = array_type(builtin_type(BuiltinType::U8), 1);
                type->array_bound = std::move(bound);
                next->context = ProvenanceOwnershipTestAccess::alias_context(type);
            }
            root = std::move(next);
        }
        { NoAllocation guard; root.reset(); }
        require(leaf.expired());
    }
    {
        // Keep a queued tag alive through a weak observer, then copy/move its
        // payload. An immutable captured source and nominal identity stay put.
        auto queued = std::make_shared<TagBinding>();
        queued->kind = TagBinding::Kind::Structure;
        queued->role = TagBinding::Role::Declaration;
        queued->spelling = "retained";
        queued->type.identity = std::make_shared<NominalTypeIdentity>();
        queued->declaration_source = std::make_shared<TagBinding>();
        const auto identity = queued->type.identity;
        const auto source = queued->declaration_source;
        std::weak_ptr<const TagBinding> weak = queued;
        std::shared_ptr<const TagBinding> retained;
        std::shared_ptr<TagBinding> copied;
        auto trigger = std::shared_ptr<const TagBinding>(new TagBinding,
            [&](const TagBinding* value) {
                retained = weak.lock();
                require(retained && retained->declaration_source == source);
                copied = std::make_shared<TagBinding>(*retained);
                TagBinding moved(std::move(*copied));
                *copied = moved;
                TagBinding assigned;
                assigned = *retained;
                *copied = std::move(assigned);
                delete value;
            });
        // Public token provenance uses normal last-owner release. Both tag
        // parents below retain one shared ancestor, preserving the live DAG.
        auto first = std::make_shared<TagBinding>();
        first->declaration_source = std::move(queued);
        auto second = std::make_shared<TagBinding>();
        second->declaration_source = std::move(trigger);
        auto root = std::make_shared<SyntaxNode>();
        root->tokens.resize(2);
        root->tokens[0].origin.tag_binding = std::move(first);
        root->tokens[1].origin.tag_binding = std::move(second);
        root.reset();
        for (const auto* value : {retained.get(), static_cast<const TagBinding*>(copied.get())})
            require(value && value->spelling == "retained" && value->type.identity == identity &&
                    value->declaration_source == source);
        { NoAllocation guard; retained.reset(); copied.reset(); }
        require(weak.expired());
    }
    {
        auto child = std::shared_ptr<TagBinding>(new TagBinding, destroy_and_poison<TagBinding>);
        std::weak_ptr<const TagBinding> weak = child;
        auto first = std::make_shared<TagBinding>();
        auto second = std::make_shared<TagBinding>();
        first->declaration_source = child;
        second->declaration_source = child;
        auto left = std::make_shared<TagBinding>();
        auto right = std::make_shared<TagBinding>();
        left->declaration_source = std::shared_ptr<const TagBinding>(first, child.get());
        right->declaration_source = std::shared_ptr<const TagBinding>(second, child.get());
        auto root = std::make_shared<SyntaxNode>();
        root->tokens.resize(2);
        root->tokens[0].origin.tag_binding = std::move(left);
        root->tokens[1].origin.tag_binding = std::move(right);
        child.reset(); first.reset(); second.reset();
        { NoAllocation guard; root.reset(); }
        require(weak.expired());
    }
    std::cout << "five deep provenance paths and weak/copy/move/alias controls passed\n";
}
