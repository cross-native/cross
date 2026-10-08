// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"

#include <limits>
#include <optional>

namespace cross {

inline const char* vector_element_error(const TypePtr& element) {
    if (element && element->kind == Type::Kind::Generic) return nullptr;
    if (!element || (!is_integer(element) && !is_floating(element)) || element->is_atomic ||
        element->builtin == BuiltinType::Bool || element->builtin == BuiltinType::F80 ||
        element->builtin == BuiltinType::F128 || element->builtin == BuiltinType::Fptr)
        return "vector element type must be a supported integer, f32, or f64 scalar";
    return nullptr;
}

struct VectorBoundValue {
    std::optional<std::uint32_t> lanes;
    const char* error{};
};

inline VectorBoundValue vector_bound_value(const TypePtr& vector,
    const Expr::IntegerConstant& value, unsigned address_bits) {
    const auto type = value.type;
    const auto bits = type == BuiltinType::Iptr || type == BuiltinType::Uptr
        ? address_bits : type_bits(builtin_type(type));
    const bool signed_value = type == BuiltinType::I8 || type == BuiltinType::I16 ||
        type == BuiltinType::I32 || type == BuiltinType::I64 ||
        type == BuiltinType::I128 || type == BuiltinType::Iptr;
    if (!bits || (signed_value && (shift_right(value.value, bits - 1).low & 1U)) ||
        value.value == UInt128{})
        return {{}, "vector attribute requires a positive integer argument"};
    if (value.value.high) return {{}, "vector lane count is out of range"};
    auto lanes = value.value.low;
    if (vector->vector_bound_unit == Type::VectorBoundUnit::Bytes) {
        const auto& element = vector->element;
        const auto element_bits = element->builtin == BuiltinType::Iptr || element->builtin == BuiltinType::Uptr
            ? address_bits : type_bits(element);
        if (!element_bits || element_bits % 8 || lanes % (element_bits / 8))
            return {{}, "vector size must be a multiple of the element size"};
        lanes /= element_bits / 8;
    }
    if (!lanes || lanes > std::numeric_limits<std::uint32_t>::max())
        return {{}, "vector lane count is out of range"};
    return {static_cast<std::uint32_t>(lanes), nullptr};
}

inline bool deferred_vector_extent(const TypePtr& type) {
    return type && type->kind == Type::Kind::Vector && !type->lanes &&
        type->vector_extent_dependency == Type::VectorExtentDependency::ExpansionContext;
}

inline bool compatible_vector_shape(const TypePtr& left, const TypePtr& right) {
    if (left->scalable != right->scalable) return false;
    if (left->lanes == right->lanes) return true;
    return (left->lanes || deferred_vector_extent(left)) &&
        (right->lanes || deferred_vector_extent(right)) &&
        (deferred_vector_extent(left) || deferred_vector_extent(right));
}

// A derived numeric/mask type keeps the unknown shape, not the original byte
// bound: promotions can change element size without changing the lane count.
inline TypePtr vector_result_type(TypePtr element, const TypePtr& shape) {
    auto result = vector_type(std::move(element), shape->lanes, shape->scalable);
    result->vector_extent_dependency = shape->vector_extent_dependency;
    return result;
}

} // namespace cross
