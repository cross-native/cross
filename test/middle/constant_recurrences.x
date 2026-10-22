// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Signed cases run with -fwrapv. The reference uses bit-serial unsigned
// multiplication with a run-time multiplier; the fixed vectors were also
// computed independently with arbitrary-precision integer arithmetic.
#define HASH(T, C, NAME) \
    [[noinline, runtime_only]] global T NAME(in const u8 *a, in uptr n, in T h) { \
        for (uptr i = 0; i < n; ++i) h = h * (T)(C) + (T)a[i]; \
        return h; \
    }

HASH(u32, 9u32, hash_u32_9)
HASH(u32, 31u32, hash_u32_31)
HASH(u32, 33u32, hash_u32_33)
HASH(u32, 65599u32, hash_u32_65599)
HASH(u32, 16777619u32, hash_u32_16777619)
HASH(u64, 31u64, hash_u64_31)
HASH(u64, 33u64, hash_u64_33)
HASH(u64, 65599u64, hash_u64_65599)
HASH(u64, 16777619u64, hash_u64_16777619)
HASH(i32, 31i32, hash_i32_31)
HASH(i32, 33i32, hash_i32_33)
HASH(i32, 65599i32, hash_i32_65599)
HASH(i32, 16777619i32, hash_i32_16777619)
HASH(u32, 1313u32, hash_u32_1313)
HASH(u32, 1103515245u32, hash_u32_lcg)

// A carried chain deeper than the recurrence walk follows.
[[noinline, runtime_only]] global u32 deep_chain(in const u8 *a, in uptr n, in u32 h) {
    for (uptr i = 0; i < n; ++i) {
        const u32 t = h ^ (h >> 7u32);
        u32 r = (t + (u32)a[i]) + h;
        r = r ^ (r >> 1u32);
        r = r ^ (r >> 2u32);
        r = r ^ (r >> 3u32);
        r = r ^ (r >> 4u32);
        r = r ^ (r >> 5u32);
        r = r ^ (r >> 6u32);
        r = r ^ (r >> 7u32);
        r = r ^ (r >> 8u32);
        r = r ^ (r >> 9u32);
        r = r ^ (r >> 10u32);
        r = r ^ (r >> 11u32);
        r = r ^ (r >> 12u32);
        r = r ^ (r >> 13u32);
        r = r ^ (r >> 14u32);
        r = r ^ (r >> 15u32);
        r = r ^ (r >> 16u32);
        r = r ^ (r >> 17u32);
        r = r ^ (r >> 18u32);
        r = r ^ (r >> 19u32);
        r = r ^ (r >> 20u32);
        r = r ^ (r >> 21u32);
        r = r ^ (r >> 22u32);
        r = r ^ (r >> 23u32);
        r = r ^ (r >> 24u32);
        r = r ^ (r >> 25u32);
        r = r ^ (r >> 26u32);
        r = r ^ (r >> 27u32);
        r = r ^ (r >> 28u32);
        r = r ^ (r >> 29u32);
        r = r ^ (r >> 30u32);
        r = r ^ (r >> 31u32);
        r = r ^ (r >> 1u32);
        r = r ^ (r >> 2u32);
        r = r ^ (r >> 3u32);
        r = r ^ (r >> 4u32);
        r = r ^ (r >> 5u32);
        r = r ^ (r >> 6u32);
        r = r ^ (r >> 7u32);
        r = r ^ (r >> 8u32);
        r = r ^ (r >> 9u32);
        r = r ^ (r >> 10u32);
        r = r ^ (r >> 11u32);
        r = r ^ (r >> 12u32);
        r = r ^ (r >> 13u32);
        r = r ^ (r >> 14u32);
        r = r ^ (r >> 15u32);
        r = r ^ (r >> 16u32);
        r = r ^ (r >> 17u32);
        r = r ^ (r >> 18u32);
        r = r ^ (r >> 19u32);
        r = r ^ (r >> 20u32);
        r = r ^ (r >> 21u32);
        r = r ^ (r >> 22u32);
        r = r ^ (r >> 23u32);
        r = r ^ (r >> 24u32);
        r = r ^ (r >> 25u32);
        r = r ^ (r >> 26u32);
        r = r ^ (r >> 27u32);
        r = r ^ (r >> 28u32);
        r = r ^ (r >> 29u32);
        r = r ^ (r >> 30u32);
        r = r ^ (r >> 31u32);
        h = r + t;
    }
    return h;
}

[[noinline, runtime_only]] global u32 reference_deep(in const u8 *a, in uptr n, in u32 h) {
    for (uptr i = 0; i < n; ++i) {
        const u32 t = h ^ (h >> 7u32);
        u32 r = (t + (u32)a[i]) + h;
        for (u32 k = 0u32; k < 62u32; ++k) r = r ^ (r >> (k % 31u32 + 1u32));
        h = r + t;
    }
    return h;
}

[[noinline, runtime_only]] global u32 hash_shifted(in const u8 *a, in uptr n,
                                                 in u32 h) {
    for (uptr i = 0; i < n; ++i) h = ((h << 5u32) + h) + (u32)a[i];
    return h;
}

[[noinline, runtime_only]] global u32 hash_terms(in const u8 *a, in uptr n,
                                               in u32 h, in u32 salt) {
    for (uptr i = 0; i < n; ++i) h = h * 33u32 + (u32)a[i] + salt - 7u32;
    return h;
}

[[noinline, runtime_only]] global u64 hash_subtract(in const u8 *a, in uptr n,
                                                  in u64 h, in u64 salt) {
    for (uptr i = 0; i < n; ++i) h = h * 31u64 - (u64)a[i] + salt;
    return h;
}

// Multiple users of the product must observe its original value.
[[noinline, runtime_only]] global u32 hash_observed(in const u8 *a, in uptr n,
                                                  in u32 h, in u32 *seen) {
    for (uptr i = 0; i < n; ++i) {
        u32 product = h * 33u32;
        seen[i] = product;
        h = product + (u32)a[i];
    }
    return h;
}

// This product is in the loop but outside its carried dependency.
[[noinline, runtime_only]] global u32 independent_product(in const u8 *a,
                                                        in uptr n) {
    u32 sum = 0u32;
    for (uptr i = 0; i < n; ++i) sum += (u32)a[i] * 33u32;
    return sum;
}

[[noinline, runtime_only]] global u64 reference_product(in u64 a, in u64 b) {
    u64 result = 0u64;
    while (b != 0u64) {
        if ((b & 1u64) != 0u64) result += a;
        a <<= 1u32;
        b >>= 1u32;
    }
    return result;
}

[[noinline, runtime_only]] global u64 reference_hash(in const u8 *a, in uptr n,
    in u64 h, in u64 multiplier) {
    for (uptr i = 0; i < n; ++i) {
        h = reference_product(h, multiplier) + (u64)a[i];
    }
    return h;
}

global i32 recurrence_entry() {
    u8 data[257];
    u32 seen[257];
    for (uptr i = 0; i < 257; ++i) data[i] = (u8)(i * 73 + (i >> 2) * 19 + 165);
    const uptr lengths[12] = {0, 1, 2, 3, 7, 8, 15, 16, 31, 64, 129, 257};
    const u64 seeds[6] = {0u64, 1u64, 5381u64, 0x80000000u64,
        0x8000000000000000u64, 0xffffffffffffffffu64};
    if ((u32)hash_u32_31(data, 257, (u32)5381u32) != 0x72c48480u32) return 1;
    if ((u32)hash_u32_33(data, 257, (u32)5381u32) != 0xe1c8fa0au32) return 2;
    if ((u32)hash_u32_65599(data, 257, (u32)5381u32) != 0xd8e01d20u32) return 3;
    if ((u32)hash_u32_16777619(data, 257, (u32)5381u32) != 0x6c4c5ec4u32) return 4;
    if ((u64)hash_u64_31(data, 257, (u64)5381u32) != 0xf4af1fef72c48480u64) return 5;
    if ((u64)hash_u64_33(data, 257, (u64)5381u32) != 0x38db121be1c8fa0au64) return 6;
    if ((u64)hash_u64_65599(data, 257, (u64)5381u32) != 0x88909402d8e01d20u64) return 7;
    if ((u64)hash_u64_16777619(data, 257, (u64)5381u32) != 0xde2855ee6c4c5ec4u64) return 8;
    if ((u32)hash_i32_31(data, 257, (i32)5381u32) != 0x72c48480u32) return 9;
    if ((u32)hash_i32_33(data, 257, (i32)5381u32) != 0xe1c8fa0au32) return 10;
    if ((u32)hash_i32_65599(data, 257, (i32)5381u32) != 0xd8e01d20u32) return 11;
    if ((u32)hash_i32_16777619(data, 257, (i32)5381u32) != 0x6c4c5ec4u32) return 12;
    for (uptr s = 0; s < 6; ++s) {
        for (uptr j = 0; j < 12; ++j) {
            uptr n = lengths[j];
            u64 seed = seeds[s];
            if ((u32)hash_u32_31(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 31u64)) return 21;
            if ((u32)hash_u32_33(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 33u64)) return 22;
            if ((u32)hash_u32_65599(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 65599u64)) return 23;
            if ((u32)hash_u32_16777619(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 16777619u64)) return 24;
            if ((u64)hash_u64_31(data, n, (u64)seed) !=
                (u64)reference_hash(data, n, seed, 31u64)) return 25;
            if ((u64)hash_u64_33(data, n, (u64)seed) !=
                (u64)reference_hash(data, n, seed, 33u64)) return 26;
            if ((u64)hash_u64_65599(data, n, (u64)seed) !=
                (u64)reference_hash(data, n, seed, 65599u64)) return 27;
            if ((u64)hash_u64_16777619(data, n, (u64)seed) !=
                (u64)reference_hash(data, n, seed, 16777619u64)) return 28;
            if ((u32)hash_i32_31(data, n, (i32)seed) !=
                (u32)reference_hash(data, n, seed, 31u64)) return 29;
            if ((u32)hash_i32_33(data, n, (i32)seed) !=
                (u32)reference_hash(data, n, seed, 33u64)) return 30;
            if ((u32)hash_i32_65599(data, n, (i32)seed) !=
                (u32)reference_hash(data, n, seed, 65599u64)) return 31;
            if ((u32)hash_i32_16777619(data, n, (i32)seed) !=
                (u32)reference_hash(data, n, seed, 16777619u64)) return 32;
            if (hash_u32_9(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 9u64)) return 46;
            if (hash_u32_1313(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 1313u64)) return 47;
            if (hash_u32_lcg(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 1103515245u64)) return 48;
            if (deep_chain(data, n, (u32)seed) != reference_deep(data, n, (u32)seed))
                return 49;
            if (hash_shifted(data, n, (u32)seed) !=
                (u32)reference_hash(data, n, seed, 33u64)) return 40;
            u32 h = (u32)seed;
            u64 wide = seed;
            u32 sum = 0u32;
            for (uptr i = 0; i < n; ++i) {
                h = (u32)reference_product((u64)h, 33u64) + (u32)data[i] + 19u32 - 7u32;
                wide = reference_product(wide, 31u64) - (u64)data[i] + 19u64;
                sum += (u32)reference_product((u64)data[i], 33u64);
            }
            if (hash_terms(data, n, (u32)seed, 19u32) != h) return 41;
            if (hash_subtract(data, n, seed, 19u64) != wide) return 42;
            if (independent_product(data, n) != sum) return 43;
            if (hash_observed(data, n, (u32)seed, seen) !=
                (u32)reference_hash(data, n, seed, 33u64)) return 44;
            h = (u32)seed;
            for (uptr i = 0; i < n; ++i) {
                u32 product = (u32)reference_product((u64)h, 33u64);
                if (seen[i] != product) return 45;
                h = product + (u32)data[i];
            }
        }
    }
    return 0;
}
