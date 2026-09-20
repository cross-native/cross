// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstring>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

extern "C" CROSS_ABI int floating_evaluation_entry();
extern "C" float tie_even, tie_up, third, underflow, signed_zero;
extern "C" float through_call, aggregate[2];
extern "C" float rounded_integer, positive_infinity, quiet_nan;
extern "C" std::int32_t truncated_float;
extern "C" double mixed_integer;
extern "C" double layout_sum, layout_aggregate[1];

template <typename Bits, typename Number>
Bits representation(Number value) {
    Bits bits{};
    static_assert(sizeof bits == sizeof value);
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

int main() {
    if (representation<std::uint32_t>(tie_even) != 0x3f800000U ||
        representation<std::uint32_t>(tie_up) != 0x3f800001U ||
        representation<std::uint32_t>(third) != 0x3eaaaaabU ||
        representation<std::uint32_t>(underflow) != 0 ||
        representation<std::uint32_t>(signed_zero) != 0x80000000U ||
        representation<std::uint32_t>(through_call) != 0x3fc00000U ||
        representation<std::uint32_t>(aggregate[0]) != 0x3fc00000U ||
        representation<std::uint32_t>(aggregate[1]) != 0x40100000U ||
        representation<std::uint64_t>(mixed_integer) != 0x4340000000000000ULL ||
        representation<std::uint64_t>(layout_sum) != 0x4030800000000000ULL ||
        representation<std::uint64_t>(layout_aggregate[0]) != 0x4030800000000000ULL ||
        truncated_float != -1 ||
        representation<std::uint32_t>(rounded_integer) != 0x4b800000U ||
        representation<std::uint32_t>(positive_infinity) != 0x7f800000U ||
        (representation<std::uint32_t>(quiet_nan) & 0x7fffffffU) <= 0x7f800000U)
        return 1;
    return floating_evaluation_entry() == 1 ? 0 : 2;
}
