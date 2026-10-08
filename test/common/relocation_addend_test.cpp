// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/relocation_addend.hpp"

#include <cstdlib>
#include <iostream>

namespace {
using namespace cross;
void require(bool valid, const char* message) {
    if (valid) return;
    std::cerr << message << '\n';
    std::exit(1);
}
}

int main() {
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    for (const auto value : {minimum, minimum + 1, std::int64_t{-1}, std::int64_t{0}, maximum})
        require(relocation_addend_i64(relocation_addend(value)) == value, "signed addend roundtrip");
    for (const unsigned bits : {8U, 16U, 32U, 64U, 128U}) {
        const auto all = mask_to(bit_not(UInt128{}), bits);
        require(relocation_addend(all, {bits, true}) == RelocationAddend{UInt128{1}, true},
                "signed source width lost");
        require(relocation_addend(all, {bits, false}) == RelocationAddend{all, false},
                "unsigned source width lost");
        const auto edge = shift_left(UInt128{1}, bits - 1);
        require(relocation_addend_fits_signed({edge, true}, bits) &&
                !relocation_addend_fits_signed({edge, false}, bits), "signed boundary lost");
    }
    const auto subtract_boundary = offset_relocation_addend({}, {UInt128{std::uint64_t{1} << 63}, false}, 1, false);
    require(subtract_boundary && relocation_addend_i64(*subtract_boundary) == minimum,
            "representable subtraction of unsigned magnitude rejected");
    const auto cancel = offset_relocation_addend(relocation_addend(minimum),
                                                {UInt128{~std::uint64_t{}}, false}, 1, true);
    require(cancel && relocation_addend_i64(*cancel) == maximum, "opposite signed magnitudes did not cancel");
    const auto overflow = offset_relocation_addend(relocation_addend(maximum), {UInt128{1}, false}, 1, true);
    require(overflow && !relocation_addend_i64(*overflow), "relocation encoding overflow accepted");
    const auto scale = offset_relocation_addend(relocation_addend(24), relocation_addend(-2), 8, true);
    require(scale && relocation_addend_i64(*scale) == 8, "typed pointer scale/sign lost");
    const auto zero = offset_relocation_addend(relocation_addend(-16), relocation_addend(-2), 8, false);
    require(zero && *zero == RelocationAddend{}, "negative zero not normalized");
    require(!offset_relocation_addend({}, {bit_not(UInt128{}), false}, 2, true),
            "128-bit scaling overflow accepted");
    require(!offset_relocation_addend({bit_not(UInt128{}), false}, {UInt128{1}, false}, 1, true),
            "128-bit addition overflow accepted");
    require(!offset_relocation_addend({}, {}, 0, true), "incomplete pointee scale accepted");
}
