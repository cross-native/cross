// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A vectorized u64 reduction multiplies by a constant whether or not the
// selected ISA has a packed 64-bit multiply. qword_multiply_entry returns
// zero when the vectorized sum matches the sum of separately computed terms.

global u64 select_sum(in const u64 *data, in uptr count) {
    u64 result = 0u64;
    uptr index = 0;
    while (index < count) {
        u64 value = data[index];
        if (value < 9223372036854775808u64) {
            result = result + value * 5u64;
        } else {
            result = result + (value ^ 11400714819323198485u64);
        }
        index = index + 1;
    }
    return result;
}

[[noinline]] global u64 term(in u64 value) {
    return value < 9223372036854775808u64 ? value * 5u64
                                          : value ^ 11400714819323198485u64;
}

global u64 qword_multiply_entry() {
    u64 data[37];
    u64 seed = 88172645463325252u64;
    u64 expected = 0u64;
    for (uptr index = 0; index < 37; ++index) {
        seed = seed ^ (seed << 13);
        seed = seed ^ (seed >> 7);
        seed = seed ^ (seed << 17);
        data[index] = seed;
        expected = expected + term(seed);
    }
    for (uptr count = 0; count <= 37; count += 6) {
        u64 partial = 0u64;
        for (uptr index = 0; index < count; ++index) {
            partial = partial + term(data[index]);
        }
        if (select_sum(data, count) != partial) return 1u64;
    }
    return select_sum(data, 37) == expected ? 0u64 : 2u64;
}
