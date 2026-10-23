// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/operand_encoders.hpp"

#include <bit>

namespace cross::encoders {

std::optional<std::uint32_t> bitmask(std::uint64_t value, unsigned bits) {
    const std::uint64_t all = bits == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << bits) - 1;
    if ((value & ~all) != 0 || value == 0 || value == all) return std::nullopt;
    // The smallest element that the value repeats.
    unsigned size = bits;
    while (size > 2) {
        const auto half = size / 2;
        const auto mask = (std::uint64_t{1} << half) - 1;
        if ((value & mask) != ((value >> half) & mask)) break;
        size = half;
    }
    const auto mask = size == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << size) - 1;
    const auto element = value & mask;
    // The element rotated right by `rotation` is 0...01...1 with `ones` ones.
    unsigned rotation = 0;
    unsigned ones = 0;
    const auto run = [](std::uint64_t x) { return x != 0 && ((x + (x & (~x + 1))) & x) == 0; };
    if (run(element)) {
        rotation = static_cast<unsigned>(std::countr_zero(element));
        ones = static_cast<unsigned>(std::countr_one(element >> rotation));
    } else {
        const auto inverted = ~element & mask;
        if (!run(inverted)) return std::nullopt;
        // The ones wrap around: they start where the zeros end.
        const auto zeros_start = static_cast<unsigned>(std::countr_zero(inverted));
        const auto zeros = static_cast<unsigned>(std::countr_one(inverted >> zeros_start));
        rotation = zeros_start + zeros;
        ones = size - zeros;
    }
    const auto immr = (size - rotation) & (size - 1);
    // imms holds the element size in its leading ones and ones - 1 below.
    const auto imms = ((~(size - 1) << 1) | (ones - 1)) & 0x3f;
    const auto n = size == 64 ? 1U : 0U;
    return (n << 12) | (immr << 6) | imms;
}

std::optional<std::uint32_t> fp8(double value) {
    const auto bits = std::bit_cast<std::uint64_t>(value);
    const auto sign = static_cast<std::uint32_t>(bits >> 63);
    const auto exponent = static_cast<std::int64_t>((bits >> 52) & 0x7ff) - 1023;
    const auto fraction = bits & ((std::uint64_t{1} << 52) - 1);
    if ((fraction & ((std::uint64_t{1} << 48) - 1)) != 0 || exponent < -3 || exponent > 4) {
        return std::nullopt;
    }
    const auto field = static_cast<std::uint32_t>(((exponent + 3) & 7) ^ 4);
    return (sign << 7) | (field << 4) | static_cast<std::uint32_t>(fraction >> 48);
}

std::optional<std::uint32_t> shifted(std::uint64_t value, unsigned bits, unsigned shift) {
    const auto limit = std::uint64_t{1} << bits;
    if (value < limit) return static_cast<std::uint32_t>(value);
    if ((value & ((std::uint64_t{1} << shift) - 1)) == 0 && (value >> shift) < limit) {
        return static_cast<std::uint32_t>((std::uint64_t{1} << bits) | (value >> shift));
    }
    return std::nullopt;
}

std::optional<std::uint32_t> wide(std::uint64_t value, unsigned bits) {
    if (bits < 64 && (value >> bits) != 0) return std::nullopt;
    for (unsigned piece = 0; piece < bits / 16; ++piece) {
        if ((value & ~(std::uint64_t{0xffff} << (16 * piece))) == 0) {
            return (piece << 16) | static_cast<std::uint32_t>(value >> (16 * piece));
        }
    }
    return std::nullopt;
}

std::optional<std::int64_t> scaled(std::int64_t value, std::int64_t scale, std::int64_t minimum,
                                   std::int64_t maximum) {
    if (value % scale != 0 || value / scale < minimum || value / scale > maximum) {
        return std::nullopt;
    }
    return value / scale;
}

std::int64_t page(std::uint64_t target, std::uint64_t place) {
    return static_cast<std::int64_t>((target & ~std::uint64_t{0xfff}) -
                                     (place & ~std::uint64_t{0xfff})) /
           4096;
}

} // namespace cross::encoders
