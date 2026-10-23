// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Records and arrays as values under both ABIs: copies, and arguments and
// results in registers, on the stack, by reference, and through a result
// address. test_main returns 0, or the number of the failed check.

volatile u32 zero = 0u32;

struct small {
    u8 tag;
    u16 count;
    u32 value;
};

struct pair {
    u64 first;
    u64 second;
};

struct odd {
    u8 bytes[3];
};

struct big {
    u32 words[9];
    u8 last;
};

struct huge {
    u64 values[40];
};

[[noinline]] static struct small make_small(in u32 seed) {
    struct small s;
    s.tag = seed;
    s.count = seed * 3u32;
    s.value = seed * 1000u32;
    return s;
}

[[noinline]] static u32 sum_small(in struct small s) {
    return s.tag + s.count + s.value;
}

[[abi("aapcs64"), noinline]] static struct small c_make_small(in u32 seed) {
    struct small s;
    s.tag = seed + 1u32;
    s.count = seed * 5u32;
    s.value = seed * 7u32;
    return s;
}

[[abi("aapcs64"), noinline]] static u32 c_sum_small(in struct small s) {
    return s.tag + s.count + s.value;
}

[[noinline]] static struct pair swap(in struct pair p) {
    struct pair q;
    q.first = p.second;
    q.second = p.first;
    return q;
}

// The tail callee's result stays in the result registers.
[[noinline]] static struct pair forward_swap(in struct pair p) {
    [[musttail]] return swap(p);
}

[[abi("aapcs64"), noinline]] static struct pair c_swap(in struct pair p) {
    struct pair q;
    q.first = p.second + 1u64;
    q.second = p.first;
    return q;
}

[[noinline]] static struct odd rotate(in struct odd o) {
    struct odd r;
    r.bytes[0] = o.bytes[1];
    r.bytes[1] = o.bytes[2];
    r.bytes[2] = o.bytes[0];
    return r;
}

[[noinline]] static struct big fill_big(in u32 seed) {
    struct big b;
    for (u32 i = 0u32; i < 9u32; i = i + 1u32) b.words[i] = seed + i;
    b.last = seed;
    return b;
}

[[noinline]] static u32 sum_big(in struct big b) {
    u32 total = b.last;
    for (u32 i = 0u32; i < 9u32; i = i + 1u32) total = total + b.words[i];
    return total;
}

[[abi("aapcs64"), noinline]] static struct big c_fill_big(in u32 seed) {
    struct big b;
    for (u32 i = 0u32; i < 9u32; i = i + 1u32) b.words[i] = seed * i;
    b.last = 7u8;
    return b;
}

[[abi("aapcs64"), noinline]] static u32 c_sum_big(in struct big b, in u32 bias) {
    u32 total = b.last + bias;
    for (u32 i = 0u32; i < 9u32; i = i + 1u32) total = total + b.words[i];
    return total;
}

// After the argument registers run out, records go on the stack.
[[abi("aapcs64"), noinline]] static u64 c_late(in u64 a, in u64 b, in u64 c, in u64 d,
                                               in u64 e, in u64 f, in u64 g,
                                               in struct pair p, in struct small s) {
    return a + b + c + d + e + f + g + p.first * 3u64 + p.second + s.value;
}

[[noinline]] static struct huge scale(in struct huge h, in u64 factor) {
    struct huge r;
    for (u32 i = 0u32; i < 40u32; i = i + 1u32) r.values[i] = h.values[i] * factor;
    return r;
}

[[noinline]] static u64 pick(in bool c, in struct pair x, in struct pair y) {
    struct pair p = c ? x : y;
    return p.first - p.second;
}

// Records that exchange values on every iteration.
[[noinline]] static u64 exchange(in struct pair x, in struct pair y, in u32 n) {
    struct pair a = x;
    struct pair b = y;
    for (u32 i = 0u32; i < n; i = i + 1u32) {
        struct pair t = a;
        a = b;
        b = t;
        b.first = b.first + 1u64;
    }
    return a.first * 1000u64 + a.second * 100u64 + b.first * 10u64 + b.second;
}

static struct small g_small;

global u32 test_main() {
    const u32 z = zero;
    struct small s = make_small(5u32 + z);
    if (s.tag != 5u8 || s.count != 15u16 || s.value != 5000u32) return 1u32;
    if (sum_small(s) != 5020u32) return 2u32;
    struct small c = c_make_small(4u32 + z);
    if (c.tag != 5u8 || c.count != 20u16 || c.value != 28u32) return 3u32;
    if (c_sum_small(c) != 53u32) return 4u32;
    struct pair p;
    p.first = 0x1111222233334444u64 + z;
    p.second = 0x5555666677778888u64;
    struct pair q = swap(p);
    if (q.first != 0x5555666677778888u64 || q.second != 0x1111222233334444u64) return 5u32;
    struct pair f = forward_swap(q);
    if (f.first != p.first || f.second != p.second) return 17u32;
    struct pair r = c_swap(q);
    if (r.first != 0x1111222233334445u64 || r.second != 0x5555666677778888u64) return 6u32;
    struct odd o;
    o.bytes[0] = 1u8 + (u8)z;
    o.bytes[1] = 2u8;
    o.bytes[2] = 3u8;
    struct odd t = rotate(o);
    if (t.bytes[0] != 2u8 || t.bytes[1] != 3u8 || t.bytes[2] != 1u8) return 7u32;
    struct big b = fill_big(10u32 + z);
    if (b.words[8] != 18u32 || b.last != 10u8) return 8u32;
    if (sum_big(b) != 136u32) return 9u32;
    struct big cb = c_fill_big(3u32 + z);
    if (cb.words[8] != 24u32 || cb.last != 7u8) return 10u32;
    if (c_sum_big(cb, 1u32) != 116u32) return 11u32;
    if (c_late(1u64, 2u64, 3u64, 4u64, 5u64, 6u64, 7u64 + z, q, s) !=
        28u64 + 0x5555666677778888u64 * 3u64 + 0x1111222233334444u64 + 5000u64) {
        return 12u32;
    }
    struct huge h;
    for (u32 i = 0u32; i < 40u32; i = i + 1u32) h.values[i] = i + z;
    struct huge doubled = scale(h, 2u64);
    if (doubled.values[39] != 78u64 || doubled.values[0] != 0u64 || h.values[39] != 39u64) {
        return 13u32;
    }
    struct pair one;
    one.first = 9u64 + z;
    one.second = 4u64;
    struct pair two;
    two.first = 1u64;
    two.second = 2u64;
    if (pick(z == 0u32, one, two) != 5u64 || pick(z != 0u32, one, two) != -1u64) return 15u32;
    if (exchange(one, two, 3u32 + z) != 2u64 * 1000u64 + 2u64 * 100u64 + 11u64 * 10u64 + 4u64 ||
        exchange(one, two, z) != 9u64 * 1000u64 + 4u64 * 100u64 + 1u64 * 10u64 + 2u64) {
        return 16u32;
    }
    g_small = s;
    struct small copy = g_small;
    if (copy.count != 15u16) return 14u32;
    return 0u32;
}
