// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>

extern "C" std::uint32_t indirect_test(std::uint32_t);
using Callback = std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::uint32_t, std::uint32_t, std::uint32_t,
                                   std::uint32_t);
extern "C" std::uint32_t indirect_external(Callback, std::uint32_t);
extern "C" std::uint32_t indirect_modes();
extern "C" std::uint32_t indirect_wide();
using FloatingCallback = double (*)(double, float, std::uint64_t, double,
                                    std::uint32_t);
extern "C" double indirect_float(FloatingCallback, double);
double external_float(double a, float b, std::uint64_t c, double d,
                      std::uint32_t e) {
    return a + b + c + d + e;
}
std::uint32_t external(std::uint32_t a, std::uint32_t b, std::uint32_t c,
                       std::uint32_t d, std::uint32_t e, std::uint32_t f,
                       std::uint32_t g, std::uint32_t h, std::uint32_t i,
                       std::uint32_t j) {
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j;
}
int main() {
    if (indirect_modes() != 51U) return 2;
    if (indirect_wide() != 1U) return 4;
    std::uint32_t x = 0x81234567U;
    for (unsigned n = 0; n < 512; ++n) {
        x = x * 1664525U + 1013904223U;
        const auto value =
            (x & 1U) ? (x ^ 2U) + (3U ^ 4U) + (5U ^ 6U) + (7U ^ 8U) + (9U ^ 10U)
                     : x + 2U * 3U + 3U * 5U + 4U * 7U + 5U * 11U + 6U * 13U +
                           7U * 17U + 8U * 19U + 9U * 23U + 10U * 29U;
        const auto expected = value + (x + 11U) + (x ^ 123U) + x * 17U;
        const auto actual = indirect_test(x);
        if (actual != expected) {
            std::printf("x=%u actual=%u expected=%u\n", x, actual, expected);
            return 1;
        }
        const auto external_expected =
            (x ^ 2U ^ 3U ^ 4U ^ 5U ^ 6U ^ 7U ^ 8U ^ 9U ^ 10U) + (x + 11U) +
            (x ^ 123U) + x * 17U;
        if (indirect_external(external, x) != external_expected) return 3;
        const double input =
            static_cast<double>(static_cast<std::int32_t>(x)) * 0.25;
        if (indirect_float(external_float, input) != input * 4.0 + 19.0)
            return 5;
    }
}
