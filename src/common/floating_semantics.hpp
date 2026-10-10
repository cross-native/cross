// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/uint128.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace cross::floating {

enum class Format { Binary32, Binary64, Extended80, Binary128 };
enum class Operation { Add, Subtract, Multiply, Divide };
enum class Comparison {
    Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual,
};

struct Value {
    UInt128 bits;
    Format format{Format::Binary64};
};

// The IEC 60559 exceptions and a denormal operand, which some FPUs trap on,
// as bits of an ExceptionSet.
enum class Exception : unsigned {
    Invalid = 1U << 0U,
    DivideByZero = 1U << 1U,
    Overflow = 1U << 2U,
    Underflow = 1U << 3U,
    Inexact = 1U << 4U,
    DenormalOperand = 1U << 5U,
};
using ExceptionSet = unsigned;

[[nodiscard]] constexpr ExceptionSet exception_set(Exception exception) {
    return static_cast<ExceptionSet>(exception);
}
// The models.md spelling: "invalid", "divide-by-zero", "overflow",
// "underflow", "inexact", or "denormal-operand".
[[nodiscard]] std::string_view exception_name(Exception exception);

// The environment of a profile (models.md "Profiles"): the exceptions that
// trap and whether a denormal result is replaced by a zero of its sign.
struct Environment {
    ExceptionSet traps{};
    bool flush_denormal_results{};
    friend bool operator==(const Environment&, const Environment&) = default;
};

// How a target encodes NaNs. In the IEEE 754-2008 encoding a set most
// significant fraction bit marks a quiet NaN, and the default NaN that an
// invalid operation produces sets only that bit. In the legacy MIPS encoding
// that bit marks a signaling NaN, and the default NaN sets every other
// fraction bit.
enum class NanEncoding { Ieee2008, Legacy };

// Whether a rewrite may create floating intermediate results that the source
// does not compute, as reassociation does: such a result can overflow,
// underflow, be inexact or denormal, or be an infinity that a later operation
// turns invalid, so only divide-by-zero may trap.
[[nodiscard]] bool permits_new_intermediates(const Environment& environment);
// Whether a multiply and an add may fuse: the fused operation can overflow or
// underflow where the separate operations do not.
[[nodiscard]] bool permits_contraction(const Environment& environment);

// The input is a suffix-free Cross decimal or hexadecimal literal. All
// conversions and operations use exact integer arithmetic followed by one
// target-format round-to-nearest, ties-to-even step.
//
// An operation adds the exceptions it raises to `*raised` when given one.
// Underflow means a tiny nonzero result, detected before rounding, which an
// enabled underflow trap takes even when the result is exact. A NaN result
// is the default NaN of `encoding`, which also tells signaling NaNs apart.
// Negation and the sign operations raise nothing.
[[nodiscard]] std::optional<Value> parse(std::string text, Format format);
[[nodiscard]] Value convert(Value value, Format format, NanEncoding encoding,
                            ExceptionSet* raised = nullptr);
[[nodiscard]] Value negate(Value value);
// Sign operations transfer only the sign bit, NaN payloads included.
[[nodiscard]] Value absolute(Value value);
[[nodiscard]] Value copy_sign(Value magnitude, Value sign);
[[nodiscard]] bool is_nan(Value value);
[[nodiscard]] bool signaling(Value value, NanEncoding encoding);
// The default NaN.
[[nodiscard]] Value nan(Format format, NanEncoding encoding);
[[nodiscard]] Value square_root(Value value, NanEncoding encoding,
                                ExceptionSet* raised = nullptr);
// IEEE 754 minNum and maxNum: a NaN operand yields the other operand, and
// operands that compare equal, such as zeros of either sign, yield the left.
// A signaling NaN raises invalid and a denormal operand raises a denormal
// operand exception, whichever operand is selected.
[[nodiscard]] Value min_num(Value left, Value right, NanEncoding encoding,
                            ExceptionSet* raised = nullptr);
[[nodiscard]] Value max_num(Value left, Value right, NanEncoding encoding,
                            ExceptionSet* raised = nullptr);
[[nodiscard]] Value binary(Operation operation, Value left, Value right,
                           Format result_format, NanEncoding encoding,
                           ExceptionSet* raised = nullptr);
// The relational comparisons signal on every NaN, equality only on a
// signaling one; the truth value of a floating value is its quiet comparison
// with zero.
[[nodiscard]] bool compare(Comparison comparison, Value left, Value right,
                           NanEncoding encoding,
                           ExceptionSet* raised = nullptr);
[[nodiscard]] bool nonzero(Value value);
[[nodiscard]] bool denormal(Value value);
[[nodiscard]] Value from_integer(UInt128 bits, unsigned width,
                                 bool is_signed, Format format,
                                 ExceptionSet* raised = nullptr);
[[nodiscard]] std::optional<UInt128> to_integer(
    Value value, unsigned width, bool is_signed,
    ExceptionSet* raised = nullptr);

// Applies `environment` to an operation that raised `raised` and produced
// `*result`: a denormal result flushes to a zero of its sign where the
// environment says so, which raises underflow and inexact. Returns the first
// raised exception that the environment traps, if any.
[[nodiscard]] std::optional<Exception> trap(const Environment& environment,
                                            ExceptionSet raised,
                                            Value* result = nullptr);

} // namespace cross::floating
