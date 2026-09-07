// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>

using U64 = std::uint64_t;
extern "C" U64 __attribute__((ms_abi)) checked_frame(U64, U64, U64, U64, U64);

extern "C" U64 __attribute__((ms_abi)) frame_touch(U64* values, U64 a, U64 b,
                                                 U64 c, U64 d) {
    values[0] += a + d;
    values[1] ^= b;
    values[2] += c * 5;
    return (a ^ b) + (c ^ d);
}

U64 reference(U64 a, U64 b, U64 c, U64 d, U64 e) {
    U64 x = a ^ e, y = b + d, z = c * 3;
    x += a + e;
    y ^= b;
    z += c * 5;
    U64 result = (a ^ b) + (c ^ e);
    if ((e & 1) == 0) {
        x += e + b;
        y ^= d;
        z += c * 5;
        result ^= (e ^ d) + (c ^ b);
    }
    return result + x + y + z + a + b + c + d + e;
}

int main() {
    U64 state = 0x123456789abcdef0ULL;
    for (unsigned index = 0; index < 128; ++index) {
        U64 args[5];
        for (auto& arg : args) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            arg = state;
        }
        const auto expected = reference(args[0], args[1], args[2], args[3], args[4]);
        const auto actual = checked_frame(args[0], args[1], args[2], args[3], args[4]);
        if (actual != expected) {
            std::fprintf(stderr, "frame roundtrip failed at input %u\n", index);
            return 1;
        }
    }
}
