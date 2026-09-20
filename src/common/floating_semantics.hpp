// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/uint128.hpp"

#include <optional>
#include <string>

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

// The input is a suffix-free Cross decimal or hexadecimal literal. All
// conversions and operations use exact integer arithmetic followed by one
// target-format round-to-nearest, ties-to-even step.
[[nodiscard]] std::optional<Value> parse(std::string text, Format format);
[[nodiscard]] Value convert(Value value, Format format);
[[nodiscard]] Value negate(Value value);
[[nodiscard]] Value binary(Operation operation, Value left, Value right,
                           Format result_format);
[[nodiscard]] bool compare(Comparison comparison, Value left, Value right);
[[nodiscard]] bool nonzero(Value value);
[[nodiscard]] Value from_integer(UInt128 bits, unsigned width,
                                 bool is_signed, Format format);
[[nodiscard]] std::optional<UInt128> to_integer(Value value, unsigned width,
                                                bool is_signed);

} // namespace cross::floating
