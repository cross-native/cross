// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/ast.hpp"
#include "middle/hir.hpp"

#include <cstdlib>

int main() {
    using namespace cross;
    const auto require = [](bool condition) {
        if (!condition) std::abort();
    };
    auto ordinary = pointer_type(builtin_type(BuiltinType::U32));
    auto first = pointer_type(builtin_type(BuiltinType::U32));
    auto second = pointer_type(builtin_type(BuiltinType::U32));
    first->address_space = 1;
    second->address_space = 2;

    require(!same_type(ordinary, first));
    require(!same_type(first, second));
    require(canonical_type_name(ordinary) != canonical_type_name(first));
    require(canonical_type_name(first) != canonical_type_name(second));

    auto function_one = function_type(
        builtin_type(BuiltinType::Void),
        {ParameterDecl{.location = {}, .name = "value", .type = first,
                       .mode = ParameterMode::In, .explicit_mode = false,
                       .location_name = {}}});
    auto function_two = function_type(
        builtin_type(BuiltinType::Void),
        {ParameterDecl{.location = {}, .name = "value", .type = second,
                       .mode = ParameterMode::In, .explicit_mode = false,
                       .location_name = {}}});
    require(!same_type(function_one, function_two));
    require(canonical_type_name(function_one) !=
            canonical_type_name(function_two));

    hir::Module module;
    const auto ordinary_id = module.intern_type(ordinary);
    const auto first_id = module.intern_type(first);
    const auto second_id = module.intern_type(second);
    require(ordinary_id != first_id && first_id != second_id);
    require(module.pointer_to(*module.type(ordinary_id).pointee) == ordinary_id);
    require(module.intern_type(function_one) !=
            module.intern_type(function_two));

    first->is_const = true;
    const auto qualified = module.intern_type(first);
    const auto unqualified = module.unqualified(qualified);
    require(module.type(unqualified).address_space == 1);
    require(unqualified == first_id);
}
