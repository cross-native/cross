// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/ast.hpp"
#include "middle/hir.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>

int main() {
    using namespace cross;
    const auto require = [](bool ok, const std::source_location at = std::source_location::current()) {
        if (!ok) {
            std::cerr << at.file_name() << ':' << at.line() << ": pointer join check failed\n";
            std::abort();
        }
    };
    hir::Module module;
    module.abi_names.emplace("left_abi", AbiId{1});
    module.abi_names.emplace("right_abi", AbiId{2});
    const auto check = [&](const TypePtr& a, const TypePtr& b, const TypePtr& expected,
                           const AddressSpaceJoin& spaces = {}) {
        const auto old_a = canonical_type_name(a), old_b = canonical_type_name(b);
        const auto joined = common_pointer_type(a, b, spaces);
        const auto reverse = common_pointer_type(b, a, spaces);
        require(joined.type.has_value() == static_cast<bool>(expected));
        require(reverse.type.has_value() == static_cast<bool>(expected));
        const auto ha = module.intern_type(a), hb = module.intern_type(b);
        const auto hj = module.common_pointer_type(ha, hb, spaces);
        const auto hr = module.common_pointer_type(hb, ha, spaces);
        if (hj.has_value() != static_cast<bool>(expected) || hr.has_value() != static_cast<bool>(expected))
            std::cerr << "HIR join: " << old_a << " / " << old_b << '\n';
        require(hj.has_value() == static_cast<bool>(expected));
        require(hr.has_value() == static_cast<bool>(expected));
        if (expected) {
            require(!joined.deferred && !reverse.deferred);
            require(same_type(*joined.type, expected) && same_type(*reverse.type, expected));
            require(*hj == module.intern_type(expected) && *hr == *hj);
            require(compare_pointee(a->pointee, expected->pointee) == PointeeCompatibility::Compatible);
            require(compare_pointee(b->pointee, expected->pointee) == PointeeCompatibility::Compatible);
        }
        require(canonical_type_name(a) == old_a && canonical_type_name(b) == old_b);
    };
    // All scalar CV combinations, at four pointer depths, must yield a safe
    // symmetric least qualification join rather than choosing the first arm.
    for (unsigned depth = 1; depth <= 4; ++depth) {
        for (unsigned a = 0; a < 4; ++a) for (unsigned b = 0; b < 4; ++b) {
            auto left = builtin_type(BuiltinType::U32, a & 1, a & 2);
            auto right = builtin_type(BuiltinType::U32, b & 1, b & 2);
            auto expected = builtin_type(BuiltinType::U32, (a | b) & 1, (a | b) & 2);
            for (unsigned n = 0; n < depth; ++n) {
                left = pointer_type(left);
                right = pointer_type(right);
                expected = pointer_type(expected, n + 1 < depth && a != b);
            }
            check(left, right, expected);
        }
    }
    const auto u32 = builtin_type(BuiltinType::U32);
    const auto cv32 = builtin_type(BuiltinType::U32, true, true);
    // Type composition has no 32-level language cutoff. Check source/HIR
    // parity and safe qualification propagation well beyond that boundary.
    for (const unsigned depth : {32U, 33U, 40U, 96U, 240U}) {
        auto left = builtin_type(BuiltinType::U32);
        auto right = builtin_type(BuiltinType::U32, true, true);
        auto expected = builtin_type(BuiltinType::U32, true, true);
        auto unsafe = builtin_type(BuiltinType::U32, true, true);
        for (unsigned level = 0; level < depth; ++level) {
            left = pointer_type(left);
            right = pointer_type(right);
            unsafe = pointer_type(unsafe);
            expected = pointer_type(expected, level + 1 < depth);
        }
        check(left, right, expected);
        require(compare_pointee(left->pointee, unsafe->pointee) == PointeeCompatibility::Incompatible);
        require(compare_pointee(expected->pointee, left->pointee) == PointeeCompatibility::Incompatible);
    }
    check(pointer_type(u32, true), pointer_type(u32, false, true), pointer_type(u32));
    check(pointer_type(array_type(u32, 3)), pointer_type(array_type(cv32, 3)),
          pointer_type(array_type(cv32, 3)));
    check(pointer_type(array_type(u32, 3)), pointer_type(array_type(u32, 4)), {});
    check(pointer_type(array_type(u32, 0)), pointer_type(array_type(u32, 3)), {});
    check(pointer_type(array_type(u32, 0)), pointer_type(array_type(u32, 0)), pointer_type(array_type(u32, 0)));
    check(pointer_type(u32), pointer_type(builtin_type(BuiltinType::I32)), {});
    check(pointer_type(u32), pointer_type(builtin_type(BuiltinType::U32, false, false, true)), {});
    check(pointer_type(cv32), pointer_type(builtin_type(BuiltinType::Void)),
          pointer_type(builtin_type(BuiltinType::Void, true, true)));
    check(pointer_type(pointer_type(u32)), pointer_type(pointer_type(builtin_type(BuiltinType::Void))), {});
    auto callback = function_type(u32, {});
    callback->function->abi = "left_abi";
    callback->function->result_location = "r8";
    ParameterDecl parameter;
    parameter.type = u32;
    callback->function->parameters.push_back(parameter);
    check(pointer_type(callback), pointer_type(builtin_type(BuiltinType::Void)), {});
    check(pointer_type(callback), pointer_type(callback), pointer_type(callback));
    auto explicit_auto = copy_type(callback);
    explicit_auto->function->parameters.front().location_name = "auto";
    check(pointer_type(callback), pointer_type(explicit_auto), pointer_type(callback));
    auto different = copy_type(callback);
    different->function->abi = "right_abi";
    check(pointer_type(callback), pointer_type(different), {});
    different = copy_type(callback);
    different->function->result_location = "stack+8";
    check(pointer_type(callback), pointer_type(different), {});
    auto one = std::make_shared<Type>();
    one->kind = Type::Kind::Record;
    one->nominal_name = "One";
    auto two = std::make_shared<Type>(*one);
    two->nominal_name = "Two";
    check(pointer_type(one), pointer_type(two), {});

    auto pending = array_type(u32, 0);
    pending->array_extent_dependency = Type::ArrayExtentDependency::ExpansionContext;
    const auto deferred = common_pointer_type(pointer_type(pending), pointer_type(array_type(cv32, 3)));
    require(deferred.type && deferred.deferred);
    require((*deferred.type)->pointee->lanes == 0);
    require((*deferred.type)->pointee->array_extent_dependency == Type::ArrayExtentDependency::ExpansionContext);
    require(!common_pointer_type(pointer_type(pending), pointer_type(array_type(builtin_type(BuiltinType::I32), 3))).type);
    require(!common_pointer_type(pointer_type(pending), pointer_type(array_type(u32, 0))).type);
    auto deep_pending = pending, deep_fixed = array_type(cv32, 3);
    for (unsigned level = 0; level < 96; ++level) {
        deep_pending = array_type(deep_pending, 1);
        deep_fixed = array_type(deep_fixed, 1);
    }
    const auto deep_deferred = common_pointer_type(pointer_type(deep_pending), pointer_type(deep_fixed));
    require(deep_deferred.type && deep_deferred.deferred);
    require(compare_pointee(deep_pending, (*deep_deferred.type)->pointee) == PointeeCompatibility::DeferredExtent);
    auto changed_leaf = array_type(builtin_type(BuiltinType::I32), 3);
    for (unsigned level = 0; level < 96; ++level) changed_leaf = array_type(changed_leaf, 1);
    require(!common_pointer_type(pointer_type(deep_pending), pointer_type(changed_leaf)).type);
    require(!common_pointer_type(pointer_type({}), pointer_type(u32)).type);

    auto first = pointer_type(u32), second = pointer_type(cv32), destination = pointer_type(cv32);
    first->address_space = 7;
    second->address_space = 11;
    destination->address_space = 19;
    check(first, second, {});
    const AddressSpaceJoin spaces = [](std::uint32_t a, std::uint32_t b) -> std::optional<std::uint32_t> {
        if ((a == 7 && b == 11) || (a == 11 && b == 7)) return 19;
        return {};
    };
    check(first, second, destination, spaces);
    check(pointer_type(first), pointer_type(second), {}, spaces);
}
