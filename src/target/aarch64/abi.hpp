// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Source types as AArch64 values and the ABI model classification of
// function interfaces, for selection and frame lowering. No ABI name selects
// code: placements come from the shared interpreter over the models.

#include "middle/hir.hpp"
#include "target/abi_lowering.hpp"
#include "target/subtarget.hpp"

#include <optional>
#include <span>

namespace cross::aarch64 {

[[nodiscard]] unsigned type_bits(const hir::Module& module, hir::TypeId id);
[[nodiscard]] bool is_void(const hir::Module& module, hir::TypeId id);
[[nodiscard]] bool is_floating(const hir::Module& module, hir::TypeId id);
[[nodiscard]] bool is_signed_integer(const hir::Module& module, hir::TypeId id);
// Records and arrays, which live in memory.
[[nodiscard]] bool is_aggregate(const hir::Module& module, hir::TypeId id);
[[nodiscard]] bool is_vector(const hir::Module& module, hir::TypeId id);
// Integers, bool, pointers, and labels of at most 64 bits: the values the
// backend keeps in general registers.
[[nodiscard]] bool is_general_scalar(const hir::Module& module, hir::TypeId id);
[[nodiscard]] unsigned storage_size(const hir::Module& module, hir::TypeId id,
                                    const TargetInfo& target);

// The AArch64 ABI model entry `id` names, or none.
[[nodiscard]] const AbiEntry* abi_model(AbiId id);

[[nodiscard]] AbiValue abi_value_for(const hir::Module& module, hir::TypeId id,
                                     const TargetInfo& target, const AbiEntry& abi,
                                     ValueTransport transport = ValueTransport::Direct);

// The interface of a function definition under `abi`.
[[nodiscard]] std::optional<SignatureLayout> classify_function(
    const hir::Module& module, const hir::Function& function, const Subtarget& subtarget,
    const AbiEntry& abi);

// The interface of a call through `signature`, with the call's argument
// types when given.
[[nodiscard]] SignatureClassificationResult classify_call(
    const hir::Module& module, const Subtarget& subtarget,
    const hir::FunctionSignature& signature, const AbiEntry& abi,
    std::span<const hir::TypeId> actual_types = {});

} // namespace cross::aarch64
