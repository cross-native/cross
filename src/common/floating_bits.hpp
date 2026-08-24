// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace cross {

// Target-independent IEEE 754 extended-precision encoding. `significand`
// contains the explicit integer bit and `exponent_sign` contains the sign bit
// followed by the 15-bit biased exponent.
struct Extended80Bits {
    std::uint64_t significand{};
    std::uint16_t exponent_sign{};
};

// Parses a decimal or hexadecimal Cross floating literal. An optional f80
// suffix and digit separators are accepted. Conversion is exact and
// round-to-nearest, ties-to-even; it never depends on the compiler host's
// `long double` representation.
std::optional<Extended80Bits> parse_extended80(std::string text);

} // namespace cross
