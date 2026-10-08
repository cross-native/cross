// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/callable_abi.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace cross;

void require(bool condition, const char* detail,
    std::source_location location = std::source_location::current()) {
    if (!condition) {
        std::cerr << location.line() << ": " << detail << '\n';
        std::abort();
    }
}

ParameterDecl parameter(TypePtr type) {
    ParameterDecl result;
    result.type = std::move(type);
    return result;
}

// Retain every synthetic type and release parents first. The deep test concerns
// traversal, not recursive shared_ptr destruction or beyond-limit source syntax.
struct OwnedTypes {
    std::vector<TypePtr> values;
    TypePtr keep(TypePtr type) {
        values.push_back(type);
        return type;
    }
    ~OwnedTypes() {
        for (auto iterator = values.rbegin(); iterator != values.rend(); ++iterator)
            iterator->reset();
    }
};

void ordering() {
    const auto scalar = builtin_type(BuiltinType::U16);
    const auto result = function_type(scalar, {}, false, "result-alias");
    const auto first = function_type(scalar, {}, false, "first-alias");
    const auto shared = function_type(scalar, {}, false, "shared-alias");
    const auto ignored = function_type(scalar, {}, false, "source-array-only");
    auto output = parameter(vector_type(array_type(pointer_type(first), 2), 4));
    output.name = "output";
    output.mode = ParameterMode::Out;
    output.location_name = "r11";
    output.declared_array_type = array_type(pointer_type(ignored), 3);
    auto copied = parameter(pointer_type(shared));
    copied.mode = ParameterMode::InOut;
    copied.location_name = "stack+16";
    const auto root = function_type(
        pointer_type(array_type(vector_type(pointer_type(result), 2), 3)),
        {output, copied, parameter(pointer_type(shared))}, true, "root-alias");
    root->function->result_location = "*r9";
    root->function->clobbers = {"r10", "r12"};
    root->function->stack_cleanup = "callee";
    std::vector<std::string> visited;
    const auto resolve = [&](std::string_view abi) -> std::optional<std::string> {
        visited.emplace_back(abi);
        if (abi.ends_with("-alias"))
            return std::string(abi.substr(0, abi.size() - 6)) + "-model";
        if (abi.ends_with("-model")) return std::string(abi);
        return {};
    };
    std::string unknown = "untouched";
    require(canonicalize_callable_abis(root, resolve, &unknown), "nested ABI resolution failed");
    require(visited == std::vector<std::string>{"root-alias", "result-alias", "first-alias",
                "shared-alias", "shared-model"},
            "ABI traversal reordered children or memoized a shared occurrence");
    require(unknown == "untouched" && ignored->function->abi == "source-array-only",
            "successful resolution modified diagnostics or a non-callable-identity edge");
    require(root->function->abi == "root-model" && result->function->abi == "result-model" &&
                first->function->abi == "first-model" && shared->function->abi == "shared-model",
            "resolver-selected model identities were not retained");
    require(root->function->variadic && root->function->result_location == "*r9" &&
                root->function->clobbers == std::vector<std::string>{"r10", "r12"} &&
                root->function->stack_cleanup == "callee" &&
                root->function->parameters[0].mode == ParameterMode::Out &&
                root->function->parameters[0].location_name == "r11" &&
                root->function->parameters[1].mode == ParameterMode::InOut &&
                root->function->parameters[1].location_name == "stack+16" &&
                root->function->parameters[0].declared_array_type == output.declared_array_type,
            "normalization changed endpoint, parameter or cleanup identity");
    visited.clear();
    require(canonicalize_callable_abis(root, resolve) &&
                visited == std::vector<std::string>{"root-model", "result-model", "first-model",
                    "shared-model", "shared-model"},
            "a later normalization reused an earlier traversal's results");
}

void failure_and_recovery() {
    const auto scalar = builtin_type(BuiltinType::U32);
    const auto before = function_type(scalar, {}, false, "before");
    const auto missing = function_type(scalar, {}, false, "missing");
    const auto after = function_type(scalar, {}, false, "after");
    const auto root = function_type(scalar,
        {parameter(pointer_type(before)), parameter(pointer_type(missing)), parameter(pointer_type(after))},
        false, "root");
    std::vector<std::string> visited;
    bool known{};
    const auto resolve = [&](std::string_view abi) -> std::optional<std::string> {
        visited.emplace_back(abi);
        if (abi == "missing" && !known) return {};
        if (abi.starts_with("model.")) return std::string(abi);
        return "model." + std::string(abi);
    };
    std::string unknown;
    require(!canonicalize_callable_abis(root, resolve, &unknown) && unknown == "missing" &&
                visited == std::vector<std::string>{"root", "before", "missing"},
            "unknown ABI did not stop at the first source-ordered failure");
    require(root->function->abi == "model.root" && before->function->abi == "model.before" &&
                missing->function->abi == "missing" && after->function->abi == "after",
            "failure changed the established partial-mutation contract");
    known = true;
    visited.clear();
    require(canonicalize_callable_abis(root, resolve) &&
                visited == std::vector<std::string>{"model.root", "model.before", "missing", "after"},
            "fresh normalization retained a previous unknown-ABI failure");

    bool throwing = true;
    visited.clear();
    const auto throwing_resolve = [&](std::string_view abi) -> std::optional<std::string> {
        visited.emplace_back(abi);
        if (abi == "model.missing" && throwing) throw std::runtime_error("ABI resolver failure");
        return std::string(abi);
    };
    try {
        (void)canonicalize_callable_abis(root, throwing_resolve);
        require(false, "ABI resolver exception was swallowed");
    } catch (const std::runtime_error& error) {
        require(std::string_view(error.what()) == "ABI resolver failure" &&
                    visited == std::vector<std::string>{"model.root", "model.before", "model.missing"},
                "exception identity changed or a later sibling was visited");
    }
    throwing = false;
    visited.clear();
    require(canonicalize_callable_abis(root, throwing_resolve) && visited.size() == 4,
            "fresh normalization inherited an exceptional traversal's state");
    require(canonicalize_callable_abis({}, {}) && canonicalize_callable_abis(scalar, {}),
            "null or scalar type unexpectedly requested ABI resolution");
    auto incomplete = std::make_shared<Type>();
    incomplete->kind = Type::Kind::Function;
    require(canonicalize_callable_abis(incomplete, resolve), "incomplete function type was not left opaque");
    const auto unselected = function_type(scalar, {}, false, "no-resolver");
    require(canonicalize_callable_abis(unselected, {}) && unselected->function->abi == "no-resolver",
            "an absent resolver selected a default ABI");
    const auto configured = function_type(scalar, {});
    unsigned default_queries{};
    require(canonicalize_callable_abis(configured,
                [&](std::string_view abi) -> std::optional<std::string> {
                    ++default_queries;
                    require(abi.empty(), "normalization supplied its own default ABI name");
                    return "selected-profile-model";
                }) && default_queries == 1 && configured->function->abi == "selected-profile-model",
            "the selected resolver did not own empty-name/default ABI policy");
}

void deep_types() {
    constexpr unsigned depth = 50000;
    const auto scalar = builtin_type(BuiltinType::U8);
    for (const bool result_chain : {false, true}) {
        OwnedTypes owners;
        owners.values.reserve(2U * depth);
        auto root = scalar;
        for (unsigned index = 0; index < depth; ++index) {
            const auto child = owners.keep(pointer_type(root));
            root = owners.keep(result_chain
                ? function_type(child, {parameter(scalar)}, false, "deep-alias")
                : function_type(scalar, {parameter(child)}, false, "deep-alias"));
        }
        unsigned calls{};
        const auto resolve = [&](std::string_view abi) -> std::optional<std::string> {
            ++calls;
            require(abi == "deep-alias", "deep traversal repeated or skipped a callable occurrence");
            return "deep-model";
        };
        require(canonicalize_callable_abis(root, resolve) && calls == depth,
                "deep callable ABI traversal failed or lost a child");
        for (const auto& type : owners.values)
            if (type->kind == Type::Kind::Function)
                require(type->function->abi == "deep-model", "a deep ABI alias remained unresolved");
    }
}
} // namespace

int main() {
    ordering();
    failure_and_recovery();
    deep_types();
}
