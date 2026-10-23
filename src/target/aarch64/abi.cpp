// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/aarch64/abi.hpp"

#include "model/model.hpp"

#include <algorithm>
#include <limits>

namespace cross::aarch64 {
namespace {

ScalarMode scalar_mode(const hir::Module& module, hir::TypeId id, const AbiEntry& abi) {
    const auto& type = module.type(id);
    const auto bits = static_cast<std::uint16_t>(type_bits(module, id));
    if (type.kind == hir::Type::Kind::Pointer ||
        (type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Label)) {
        return ScalarMode::pointer(static_cast<std::uint16_t>(abi.address_bits));
    }
    if (type.kind == hir::Type::Kind::Record) return ScalarMode::aggregate(bits);
    if (type.kind == hir::Type::Kind::Array) return ScalarMode::array(bits);
    if (type.kind == hir::Type::Kind::Vector) return ScalarMode::vector(bits);
    if (is_floating(module, id)) return ScalarMode::floating(bits);
    return bits == 0 ? ScalarMode::zero() : ScalarMode::integer(bits);
}

std::vector<AbiValue> parameter_values(const hir::Module& module, const TargetInfo& target,
                                       const AbiEntry& abi,
                                       const std::vector<hir::Parameter>& parameters,
                                       std::span<const hir::TypeId> actual_types) {
    std::vector<AbiValue> result;
    const auto count = actual_types.empty() ? parameters.size() : actual_types.size();
    result.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const bool fixed = index < parameters.size();
        const auto type = actual_types.empty() ? parameters[index].type : actual_types[index];
        const auto transport = fixed && parameters[index].mode != ParameterMode::In
                                   ? ValueTransport::ByReference
                                   : ValueTransport::Direct;
        result.push_back(abi_value_for(module, type, target, abi, transport));
    }
    return result;
}

} // namespace

unsigned type_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return module.address_bits;
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto bytes = module.record(*type.record).size;
        return bytes <= std::numeric_limits<unsigned>::max() / 8U
                   ? static_cast<unsigned>(bytes * 8U)
                   : 0U;
    }
    if (type.kind == hir::Type::Kind::Array || type.kind == hir::Type::Kind::Vector) {
        if (!type.element || type.lanes == 0 || type.scalable) return 0;
        const auto element = type_bits(module, *type.element);
        return element != 0 && type.lanes <= std::numeric_limits<unsigned>::max() / element
                   ? element * type.lanes
                   : 0U;
    }
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8: return 8;
    case BuiltinType::I16:
    case BuiltinType::U16: return 16;
    case BuiltinType::I32:
    case BuiltinType::U32:
    case BuiltinType::F32: return 32;
    case BuiltinType::I64:
    case BuiltinType::U64:
    case BuiltinType::F64: return 64;
    case BuiltinType::Iptr:
    case BuiltinType::Uptr:
    case BuiltinType::Fptr:
    case BuiltinType::Label: return module.address_bits;
    case BuiltinType::F80: return 80;
    case BuiltinType::I128:
    case BuiltinType::U128:
    case BuiltinType::F128: return 128;
    case BuiltinType::Void: return 0;
    }
    return 0;
}

bool is_void(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Void;
}

bool is_floating(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    return type.kind == hir::Type::Kind::Builtin &&
           (type.builtin == BuiltinType::F32 || type.builtin == BuiltinType::F64 ||
            type.builtin == BuiltinType::F80 || type.builtin == BuiltinType::F128 ||
            type.builtin == BuiltinType::Fptr);
}

bool is_signed_integer(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin) return false;
    switch (type.builtin) {
    case BuiltinType::I8:
    case BuiltinType::I16:
    case BuiltinType::I32:
    case BuiltinType::I64:
    case BuiltinType::I128:
    case BuiltinType::Iptr: return true;
    default: return false;
    }
}

bool is_aggregate(const hir::Module& module, hir::TypeId id) {
    const auto kind = module.type(id).kind;
    return kind == hir::Type::Kind::Record || kind == hir::Type::Kind::Array;
}

bool is_vector(const hir::Module& module, hir::TypeId id) {
    return module.type(id).kind == hir::Type::Kind::Vector;
}

bool is_general_scalar(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Pointer) return true;
    if (type.kind != hir::Type::Kind::Builtin || is_floating(module, id) || is_void(module, id)) {
        return false;
    }
    const auto bits = type_bits(module, id);
    return bits != 0 && bits <= 64;
}

unsigned storage_size(const hir::Module& module, hir::TypeId id, const TargetInfo& target) {
    const auto size = hir::layout_size(module, id, target).value_or(0);
    return size <= std::numeric_limits<unsigned>::max() ? static_cast<unsigned>(size) : 0U;
}

const AbiEntry* abi_model(AbiId id) {
    const auto* abi = model_registry().find_abi(id);
    return abi && abi->architecture == "aarch64" ? abi : nullptr;
}

AbiValue abi_value_for(const hir::Module& module, hir::TypeId id, const TargetInfo& target,
                       const AbiEntry& abi, ValueTransport transport) {
    AbiValue result;
    result.mode = scalar_mode(module, id, abi);
    result.transport = transport;
    // A value is transported as its type without the type's own request.
    result.alignment_bits = static_cast<std::uint16_t>(std::min<std::uint64_t>(
        hir::natural_alignment(module, id, target).value_or(1) * 8U,
        std::numeric_limits<std::uint16_t>::max()));
    const auto& type = module.type(id);
    if (type.kind == hir::Type::Kind::Record && type.record) {
        const auto& record = module.record(*type.record);
        result.union_value = record.is_union;
        std::optional<std::pair<std::uint64_t, unsigned>> bit_field_unit;
        for (const auto& member : record.members) {
            if (member.bit_width && *member.bit_width == 0) continue;
            if (member.bit_width && !record.is_union) {
                const auto unit = std::pair{member.offset, type_bits(module, member.type)};
                if (bit_field_unit == unit) continue;
                bit_field_unit = unit;
            } else {
                bit_field_unit.reset();
            }
            result.elements.push_back(abi_value_for(module, member.type, target, abi));
            result.element_offsets_bits.push_back(static_cast<std::uint32_t>(member.offset * 8U));
        }
    } else if (type.kind == hir::Type::Kind::Array && type.element) {
        auto element = abi_value_for(module, *type.element, target, abi);
        result.element_count = type.lanes;
        const auto stride = storage_size(module, *type.element, target) * 8U;
        if (stride == element.mode.bits) {
            result.elements.push_back(std::move(element));
            return result;
        }
        // Padded elements are placed explicitly at their storage stride.
        result.mode = ScalarMode::array(static_cast<std::uint16_t>(std::min<std::uint64_t>(
            std::uint64_t{stride} * type.lanes, std::numeric_limits<std::uint16_t>::max())));
        for (std::uint32_t index = 0; index < type.lanes; ++index) {
            result.elements.push_back(element);
            result.element_offsets_bits.push_back(index * stride);
        }
    }
    return result;
}

std::optional<SignatureLayout> classify_function(const hir::Module& module,
                                                 const hir::Function& function,
                                                 const Subtarget& subtarget,
                                                 const AbiEntry& abi) {
    auto signature = hir::call_signature(module, function.id, std::nullopt);
    if (!signature) return std::nullopt;
    auto classified = classify_call(module, subtarget, *signature, abi);
    if (!classified) return std::nullopt;
    return std::move(classified.layout);
}

SignatureClassificationResult classify_call(const hir::Module& module,
                                            const Subtarget& subtarget,
                                            const hir::FunctionSignature& signature,
                                            const AbiEntry& abi,
                                            std::span<const hir::TypeId> actual_types) {
    const auto arguments = parameter_values(module, subtarget.target(), abi,
                                            signature.parameters, actual_types);
    std::vector<AbiValue> results;
    if (!is_void(module, signature.result_type)) {
        results.push_back(abi_value_for(module, signature.result_type, subtarget.target(), abi));
    }
    return signature.variadic
               ? classify_variadic_signature(abi, arguments, results,
                                             signature.parameters.size(),
                                             subtarget.enabled_features())
               : classify_signature(abi, arguments, results, subtarget.enabled_features());
}

} // namespace cross::aarch64
