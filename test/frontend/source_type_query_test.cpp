// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/source_type_query.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cross;
void require(bool value, const char* message) {
    if (!value) { std::cerr << message << '\n'; std::abort(); }
}

std::unique_ptr<Expr> name(std::string text) {
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Name;
    result->text = std::move(text);
    return result;
}
std::unique_ptr<Expr> binary(std::string operation, std::unique_ptr<Expr> left,
                             std::unique_ptr<Expr> right) {
    auto result = std::make_unique<Expr>();
    result->kind = Expr::Kind::Binary;
    result->text = std::move(operation);
    result->left = std::move(left);
    result->right = std::move(right);
    return result;
}

// Tear down the synthetic 50,000-node host-infrastructure fixture iteratively.
// This test does not raise the language's parsing or evaluation depth limits.
struct Chain {
    std::unique_ptr<Expr> root;
    ~Chain() {
        while (root) {
            auto next = std::move(root->left);
            root = std::move(next);
        }
    }
};

struct TypeOwners {
    std::vector<TypePtr> values;
    TypePtr keep(TypePtr type) {
        values.push_back(type);
        return type;
    }
    ~TypeOwners() {
        for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator)
            iterator->reset();
    }
};

ParameterDecl parameter(TypePtr type) {
    ParameterDecl result;
    result.type = std::move(type);
    return result;
}

void check_meta_types() {
    const auto scalar = builtin_type(BuiltinType::U32);
    require(!contains_meta_type({}) && !contains_meta_type(scalar) &&
                !contains_meta_type(record_type("opaque")),
            "pure meta containment selected an unrelated type");
    for (const auto& meta : {tokens_type(), syntax_match_type(), syntax_type(), span_type(),
                            context_type(), bytes_type(), buffer_type()}) {
        require(is_meta_type(meta) && contains_meta_type(meta) &&
                    !is_meta_type(pointer_type(meta)) && contains_meta_type(pointer_type(meta)),
                "direct versus nested meta type classification changed");
        require(contains_meta_type(array_type(pointer_type(meta), 4)) &&
                    contains_meta_type(vector_type(pointer_type(meta), 2)) &&
                    contains_meta_type(function_type(pointer_type(meta), {})) &&
                    contains_meta_type(function_type(scalar, {parameter(pointer_type(meta))})),
                "meta type was lost through an element or callable edge");
    }
    const auto shared = function_type(scalar, {parameter(scalar)});
    const auto root = function_type(pointer_type(shared), {parameter(pointer_type(shared))});
    require(!contains_meta_type(root), "shared ordinary type acquired meta storage");
    shared->function->parameters[0].type = pointer_type(bytes_type());
    require(contains_meta_type(root), "meta containment reused a stale graph result");
    shared->function->parameters[0].type = scalar;
    shared->function->parameters[0].declared_array_type = array_type(tokens_type(), 2);
    require(!contains_meta_type(root), "containment added a source-only declared-array edge");

    // These host-constructed graphs do not raise source or evaluator limits.
    // Keep all owners so their teardown cannot masquerade as traversal recursion.
    for (const bool with_meta : {false, true}) {
        TypeOwners owners;
        owners.values.reserve(100000);
        auto deep = with_meta ? tokens_type() : scalar;
        for (unsigned index = 0; index < 50000; ++index) {
            const auto child = owners.keep(pointer_type(deep));
            deep = owners.keep(index % 2U == 0U
                ? function_type(child, {}) : function_type(scalar, {parameter(child)}));
        }
        require(contains_meta_type(deep) == with_meta,
                "deep meta containment recursed or lost a result/parameter child");
    }

    // Recursive/shared source-type graphs are already supported by copy_type.
    // A pure reachability query must terminate without imposing a source rule.
    const auto recursive = function_type(scalar, {});
    recursive->function->parameters.push_back(parameter(pointer_type(recursive)));
    require(!contains_meta_type(recursive), "a meta-free recursive graph did not terminate");
    recursive->function->parameters.push_back(parameter(context_type()));
    require(contains_meta_type(recursive), "a recursive edge hid a later reachable meta type");
    recursive->function->parameters.clear();
}
}

int main() {
    using namespace cross;
    std::vector<std::pair<std::string, bool>> visited;
    TypePtr selected = array_type(builtin_type(BuiltinType::U32), 3);
    bool failed{}, throw_leaf{};
    const auto classify = [&](const Expr& source, bool decay, const auto& type_of) -> TypePtr {
        if (source.kind == Expr::Kind::Parenthesized)
            return type_of(*source.left, decay);
        if (source.kind == Expr::Kind::Binary) {
            const auto first = type_of(*source.left, false);
            if (source.text == "either" && first) return first;
            if (source.text == "repeat") {
                // The same node requested again is a separate classification,
                // including its different decay mode. No node-global cache.
                const auto adjusted = type_of(*source.left, true);
                return adjusted;
            }
            const auto second = type_of(*source.right);
            return second;
        }
        visited.emplace_back(source.text, decay);
        if (throw_leaf && source.text == "explode") throw std::runtime_error("source type failure");
        if (source.text == "stop") { failed = true; return {}; }
        if (source.text == "unknown") return {};
        return decay && selected->kind == Type::Kind::Array
            ? pointer_type(selected->element) : selected;
    };
    const auto query = [&](const Expr& source, bool decay = true) {
        return classify_source_type(source, decay, classify, [&] { return failed; });
    };

    Chain deep{name("leaf")};
    for (unsigned i = 0; i < 50000; ++i) {
        auto wrapper = std::make_unique<Expr>();
        wrapper->kind = Expr::Kind::Parenthesized;
        wrapper->left = std::move(deep.root);
        deep.root = std::move(wrapper);
    }
    const auto raw = query(*deep.root, false);
    require(raw == selected && visited == std::vector<std::pair<std::string, bool>>{{"leaf", false}},
            "deep type classification recursed, repeated a leaf or lost decay mode");
    visited.clear();
    const auto decayed = query(*deep.root);
    require(decayed && decayed->kind == Type::Kind::Pointer && decayed->pointee == selected->element &&
            visited == std::vector<std::pair<std::string, bool>>{{"leaf", true}},
            "classification reused an earlier root's result or decay mode");

    // Completed unknown results must neither spin nor suppress the next
    // requested child. Conversely a known first type can leave a child opaque.
    visited.clear();
    auto unknown = binary("both", name("unknown"), name("right"));
    require(query(*unknown) && visited == std::vector<std::pair<std::string, bool>>{
                {"unknown", false}, {"right", true}},
            "unknown child result changed classification order");
    visited.clear();
    auto lazy = binary("either", name("left"), name("explode"));
    throw_leaf = true;
    require(query(*lazy) == selected && visited == std::vector<std::pair<std::string, bool>>{{"left", false}},
            "classification visited an unrequested subtree");
    visited.clear();
    auto repeated = binary("repeat", name("twice"), name("explode"));
    require(query(*repeated)->kind == Type::Kind::Pointer &&
            visited == std::vector<std::pair<std::string, bool>>{{"twice", false}, {"twice", true}},
            "repeated child requests were merged or reordered");

    visited.clear();
    auto throwing = binary("both", name("explode"), name("after"));
    try {
        (void)query(*throwing);
        require(false, "query exception was swallowed");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "source type failure" &&
                visited == std::vector<std::pair<std::string, bool>>{{"explode", false}},
                "exception continued a sibling query or lost its identity");
    }
    throw_leaf = false;
    visited.clear();
    auto stopped = binary("both", name("stop"), name("after"));
    require(!query(*stopped) && visited == std::vector<std::pair<std::string, bool>>{{"stop", false}},
            "resource failure continued a sibling classification");
    failed = false;
    visited.clear();
    selected = builtin_type(BuiltinType::U16);
    require(query(*throwing) == selected && visited == std::vector<std::pair<std::string, bool>>{
                {"explode", false}, {"after", true}},
            "fresh classification retained failure state or previous lexical results");
    check_meta_types();
}
