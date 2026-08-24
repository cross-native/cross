// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/target.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cross {

enum class ScalarKind {
    Integer,
    Floating,
    Pointer,
    Pair,
    Aggregate,
    Array,
    Vector,
    Zero,
};

// ScalarMode describes the source value, not its ABI carrier. For example, a
// one-bit SysV parameter has a 32-bit carrier, while its mode remains i1.
struct ScalarMode {
    ScalarKind kind{ScalarKind::Integer};
    std::uint16_t bits{};

    static constexpr ScalarMode integer(std::uint16_t width) {
        return {ScalarKind::Integer, width};
    }
    static constexpr ScalarMode pointer(std::uint16_t width = 64) {
        return {ScalarKind::Pointer, width};
    }
    static constexpr ScalarMode floating(std::uint16_t width) {
        return {ScalarKind::Floating, width};
    }
    static constexpr ScalarMode pair(std::uint16_t width) {
        return {ScalarKind::Pair, width};
    }
    static constexpr ScalarMode aggregate(std::uint16_t width) {
        return {ScalarKind::Aggregate, width};
    }
    static constexpr ScalarMode array(std::uint16_t width) {
        return {ScalarKind::Array, width};
    }
    static constexpr ScalarMode vector(std::uint16_t width) {
        return {ScalarKind::Vector, width};
    }
    static constexpr ScalarMode zero() {
        return {ScalarKind::Zero, 0};
    }
};

enum class ValueTransport {
    Direct,
    // The ABI transports the address of a distinct caller-owned channel. This
    // is the representation used by automatic Cross out/inout parameters.
    ByReference,
};

struct AbiValue {
    ScalarMode mode;
    ValueTransport transport{ValueTransport::Direct};
    std::uint16_t alignment_bits{};
    std::uint32_t element_count{};
    std::vector<AbiValue> elements;
    std::vector<std::uint32_t> element_offsets_bits;

    AbiValue() = default;
    explicit AbiValue(
        ScalarMode value_mode,
        ValueTransport value_transport = ValueTransport::Direct)
        : mode(value_mode), transport(value_transport) {}
};

enum class LocationKind {
    Register,
    Stack,
};

struct Location {
    LocationKind kind{LocationKind::Register};
    std::string reg;

    // For a call argument, this is measured from the caller's RSP immediately
    // before `call`. At callee entry, add return_address_size.
    std::size_t stack_offset{};
};

struct ValuePiece {
    Location location;
    std::uint16_t value_bit_offset{};
    std::uint16_t value_bits{};
    std::uint16_t carrier_bits{};
    // Nonzero only for an indirect piece. It is the number of source-value
    // bits addressed by this pointer channel.
    std::uint16_t indirect_value_bits{};
};

struct ArgumentAssignment {
    std::size_t argument_index{};
    AbiValue value;
    std::vector<ValuePiece> pieces;
    // Additional register views required only at a variadic boundary.  They
    // carry the same source bits and do not consume another model cursor.
    std::vector<ValuePiece> shadows;
    bool indirect{};

    // These describe the portion of the outgoing stack occupied by this
    // argument. They are zero when every piece is register-resident.
    std::size_t stack_size{};
    std::size_t stack_alignment{};
};

struct AbiCursorUsage {
    std::string cursor;
    std::size_t count{};
};

struct AbiImplicitRegisterValue {
    std::string reg;
    std::uint64_t value{};
    unsigned bits{};
};

struct CallLayout {
    std::vector<ArgumentAssignment> arguments;
    std::size_t argument_stack_base_size{};
    std::size_t register_spill_size{};

    // High-water mark before final call-frame rounding. On Microsoft this
    // includes the mandatory shadow space.
    std::size_t used_stack_size{};
    std::size_t outgoing_area_size{};
    std::size_t outgoing_area_alignment{16};

    // Filled for variadic classifications. `named_cursors` and
    // `variadic_stack_offset` describe the callee-visible state immediately
    // after the fixed prefix; `cursors` and hidden values describe the whole
    // call site after unnamed arguments have also been placed.
    std::vector<AbiCursorUsage> named_cursors;
    std::vector<AbiCursorUsage> cursors;
    std::size_t variadic_stack_offset{};
    std::vector<AbiImplicitRegisterValue> implicit_register_values;
};

enum class ClassificationError {
    None,
    UnsupportedAbi,
    InvalidScalarMode,
    SizeOverflow,
};

struct ClassificationResult {
    CallLayout layout;
    ClassificationError error{ClassificationError::None};
    std::size_t error_argument{};

    explicit constexpr operator bool() const {
        return error == ClassificationError::None;
    }
};

using AbiFeatureSet = std::span<const std::string>;

// Parameter definitions and call sites deliberately have separate entry
// points even though their placement is symmetric. Stack offsets returned for
// parameters use the caller-relative convention documented by Location.
ClassificationResult classify_parameters(const AbiEntry& abi,
                                         std::span<const AbiValue> parameters,
                                         AbiFeatureSet features = {});
ClassificationResult classify_call_arguments(const AbiEntry& abi,
                                              std::span<const AbiValue> arguments,
                                              AbiFeatureSet features = {});

struct ReturnAssignment {
    std::size_t result_index{};
    AbiValue value;
    ScalarMode mode;
    std::vector<ValuePiece> pieces;
    bool indirect{};
    // Some ABIs also return the result-area pointer. Empty means that only the
    // hidden incoming channel is observable.
    std::string indirect_result_reg;
    std::size_t stack_size{};
    std::size_t stack_alignment{};
    ClassificationError error{ClassificationError::None};

    explicit constexpr operator bool() const {
        return error == ClassificationError::None;
    }
};

ReturnAssignment classify_return(const AbiEntry& abi,
                                 const AbiValue& value,
                                 AbiFeatureSet features = {});

struct SignatureLayout {
    CallLayout call;
    std::vector<ReturnAssignment> results;
};

struct SignatureClassificationResult {
    SignatureLayout layout;
    ClassificationError error{ClassificationError::None};
    std::size_t error_value{};
    bool error_in_results{};

    explicit constexpr operator bool() const {
        return error == ClassificationError::None;
    }
};

// Classifies the complete signature in one pass. This is required by ABIs
// whose stack-result and argument-spill areas share a frame or whose hidden
// result channel consumes an ordinary argument-bank cursor.
SignatureClassificationResult classify_signature(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::span<const AbiValue> results,
    AbiFeatureSet features = {});

// Classifies a variadic call or definition prefix.  `fixed_argument_count`
// splits the named prefix from unnamed actuals and activates only declarative
// variadic effects from the selected ABI model.
SignatureClassificationResult classify_variadic_signature(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::span<const AbiValue> results, std::size_t fixed_argument_count,
    AbiFeatureSet features = {});

ClassificationResult classify_variadic_call_arguments(
    const AbiEntry& abi, std::span<const AbiValue> arguments,
    std::size_t fixed_argument_count,
    AbiFeatureSet features = {});

std::size_t abi_cursor_count(std::span<const AbiCursorUsage> cursors,
                             std::string_view cursor);

constexpr std::size_t callee_stack_offset(const ValuePiece& piece,
                                          const AbiEntry& abi) {
    return piece.location.stack_offset + abi.return_address_bytes;
}

} // namespace cross
