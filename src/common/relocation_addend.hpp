// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/integer_semantics.hpp"

#include <limits>

namespace cross {

// Mathematical displacement, never an emitted address or an integer encoding
// of one. Retain unsigned source magnitudes without host-width truncation.
struct RelocationAddend {
    UInt128 magnitude;
    bool negative{};
    bool operator==(const RelocationAddend&) const = default;
};

inline RelocationAddend relocation_addend(UInt128 value, IntegerType type) {
    value = mask_to(value, type.bits);
    const bool negative = integer_negative(value, type);
    return {negative ? mask_to(negate(value), type.bits) : value, negative};
}

inline RelocationAddend relocation_addend(std::int64_t value) {
    return relocation_addend(UInt128{static_cast<std::uint64_t>(value)}, {64, true});
}

inline std::optional<RelocationAddend> offset_relocation_addend(
    RelocationAddend base, RelocationAddend offset, std::uint64_t scale, bool addition) {
    if (!scale) return {};
    if (divide(bit_not(UInt128{}), UInt128{scale}).first < offset.magnitude) return {};
    offset.magnitude = multiply(offset.magnitude, UInt128{scale});
    offset.negative = offset.negative != !addition;
    if (base.negative == offset.negative) {
        bool overflow{};
        base.magnitude = add(base.magnitude, offset.magnitude, &overflow);
        if (overflow) return {};
    } else if (base.magnitude < offset.magnitude) {
        base.magnitude = subtract(offset.magnitude, base.magnitude);
        base.negative = offset.negative;
    } else base.magnitude = subtract(base.magnitude, offset.magnitude);
    if (base.magnitude == UInt128{}) base.negative = false;
    return base;
}

inline bool relocation_addend_fits_signed(RelocationAddend value, unsigned bits) {
    if (!bits || bits > 128) return false;
    const auto boundary = shift_left(UInt128{1}, bits - 1);
    return value.negative ? !(boundary < value.magnitude) : value.magnitude < boundary;
}

// The current source-entity and Data IR relocation records use signed 64-bit
// addends. This is an explicit compiler representation limit, not a target
// pointer width or a universal source-language restriction.
inline std::optional<std::int64_t> relocation_addend_i64(RelocationAddend value) {
    if (!relocation_addend_fits_signed(value, 64)) return {};
    if (value.negative && value.magnitude.low == (std::uint64_t{1} << 63))
        return std::numeric_limits<std::int64_t>::min();
    const auto magnitude = static_cast<std::int64_t>(value.magnitude.low);
    return value.negative ? -magnitude : magnitude;
}

} // namespace cross
