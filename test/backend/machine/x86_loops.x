// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Loop shapes from a deflate match search and a bit-serial Huffman decoder.

#define WIN 32768uptr
#define LIMIT 258uptr

// Longest match along a hash chain; `chain-- > 0` is a counted exit test
// with a side effect that runs only when the earlier tests hold.
global i32 chain_match(const u8 *src, uptr n, uptr i, const i32 *head_of, const i32 *prev,
                       u32 *dist) {
    const uptr lim = n - i < LIMIT ? n - i : LIMIT;
    uptr best = 0;
    i32 chain = 128;
    if (lim < 3uptr) return 0;
    for (i32 j = head_of[0]; j >= 0 && i - (uptr)j <= WIN && chain-- > 0; j = prev[j & 32767]) {
        const u8 *a = src + i;
        const u8 *b = src + j;
        if (b[best] == a[best]) {
            uptr l = 0;
            while (l < lim && a[l] == b[l]) l++;
            if (l > best) {
                best = l;
                *dist = (u32)(i - (uptr)j);
                if (l == lim) break;
            }
        }
        if (prev[j & 32767] >= j) break;
    }
    return best >= 3uptr ? (i32)best : 0;
}

struct Bits { const u8 *src; uptr len; uptr pos; u32 buf; i32 cnt; };
struct Huff { i16 count[16]; i16 sym[288]; };

static i32 bit(struct Bits *b) {
    if (!b->cnt) {
        if (b->pos >= b->len) return -1;
        b->buf = b->src[b->pos++];
        b->cnt = 8;
    }
    const i32 v = (i32)(b->buf & 1u32);
    b->buf >>= 1;
    b->cnt--;
    return v;
}

// Canonical Huffman decoding one bit at a time through a state record.
[[noinline]] static i32 decode(struct Bits *b, const struct Huff *h) {
    i32 code = 0, first = 0, index = 0;
    for (i32 len = 1; len <= 15; len++) {
        const i32 x = bit(b);
        if (x < 0) return -1;
        code |= x;
        const i32 count = h->count[len];
        if (code - first < count) return h->sym[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return -1;
}

global i64 decode_all(struct Bits *b, const struct Huff *h, i32 symbols) {
    i64 sum = 0;
    for (i32 k = 0; k < symbols; k++) {
        const i32 s = decode(b, h);
        if (s < 0) break;
        sum += s;
    }
    return sum;
}

// Forward phi webs, rotating loop phis, and values live on the other edge.
global u32 phi_web(const u32 *a, u32 n) {
    u32 x = 3, y = 11, z = 7;
    for (u32 k = 0; k < n; k++) {
        u32 t = a[k];
        if (t & 1u32) t = x + t;
        if (t & 2u32) t = y ^ t;
        const u32 old = x;
        x = y + t;
        y = z ^ old;
        z = t + old;
    }
    return x ^ (y << 5) ^ (z >> 3);
}

global i64 extend_signed(i32 x) { return (i64)x; }
global i64 extend_mask(i32 x) { return (i64)(x & 32767); }
global i64 extend_signbit(i32 x) { return (i64)(x & (-2147483647 - 1)); }
global u64 extend_unsigned(u32 x) { return (u64)x; }
global i64 extend_phi(u32 x, bool pick) {
    i32 v = (i32)(x & 32767u32);
    if (pick) v = (i32)(x & 65535u32);
    return (i64)v;
}
global bool flags_eq32(u32 x, u32 *result) {
    bool c = x == 17u32;
    *result = x - 17u32;
    return c;
}
global bool flags_ne32(u32 x, u32 *result) {
    bool c = x != 17u32;
    *result = x - 17u32;
    return c;
}
global bool flags_slt32(u32 x, u32 *result) {
    bool c = (i32)x < 17i32;
    *result = x - 17u32;
    return c;
}
global bool flags_sge32(u32 x, u32 *result) {
    bool c = (i32)x >= 17i32;
    *result = x - 17u32;
    return c;
}
global bool flags_sle32(u32 x, u32 *result) {
    bool c = (i32)x <= 16i32;
    *result = x - 17u32;
    return c;
}
global bool flags_sgt32(u32 x, u32 *result) {
    bool c = (i32)x > 16i32;
    *result = x - 17u32;
    return c;
}
global bool flags_ult32(u32 x, u32 *result) {
    bool c = x < 17u32;
    *result = x - 17u32;
    return c;
}
global bool flags_uge32(u32 x, u32 *result) {
    bool c = x >= 17u32;
    *result = x - 17u32;
    return c;
}
global bool flags_ule32(u32 x, u32 *result) {
    bool c = x <= 16u32;
    *result = x - 17u32;
    return c;
}
global bool flags_ugt32(u32 x, u32 *result) {
    bool c = x > 16u32;
    *result = x - 17u32;
    return c;
}
global bool result_zero32(u32 x, u32 *result) {
    const u32 y = x - 1u32;
    *result = y;
    return (i32)y == 0i32;
}
global bool result_nonzero32(u32 x, u32 *result) {
    const u32 y = x - 1u32;
    *result = y;
    return (i32)y != 0i32;
}
global bool result_negative32(u32 x, u32 *result) {
    const u32 y = x - 1u32;
    *result = y;
    return (i32)y < 0i32;
}
global bool result_positive32(u32 x, u32 *result) {
    const u32 y = x - 1u32;
    *result = y;
    return (i32)y > 0i32;
}
global bool result_nonnegative32(u32 x, u32 *result) {
    const u32 y = x - 1u32;
    *result = y;
    return (i32)y >= 0i32;
}
global bool result_nonpositive32(u32 x, u32 *result) {
    const u32 y = x - 1u32;
    *result = y;
    return (i32)y <= 0i32;
}
global bool flags_eq64(u64 x, u64 *result) {
    bool c = x == 17u64;
    *result = x - 17u64;
    return c;
}
global bool flags_ne64(u64 x, u64 *result) {
    bool c = x != 17u64;
    *result = x - 17u64;
    return c;
}
global bool flags_slt64(u64 x, u64 *result) {
    bool c = (i64)x < 17i64;
    *result = x - 17u64;
    return c;
}
global bool flags_sge64(u64 x, u64 *result) {
    bool c = (i64)x >= 17i64;
    *result = x - 17u64;
    return c;
}
global bool flags_sle64(u64 x, u64 *result) {
    bool c = (i64)x <= 16i64;
    *result = x - 17u64;
    return c;
}
global bool flags_sgt64(u64 x, u64 *result) {
    bool c = (i64)x > 16i64;
    *result = x - 17u64;
    return c;
}
global bool flags_ult64(u64 x, u64 *result) {
    bool c = x < 17u64;
    *result = x - 17u64;
    return c;
}
global bool flags_uge64(u64 x, u64 *result) {
    bool c = x >= 17u64;
    *result = x - 17u64;
    return c;
}
global bool flags_ule64(u64 x, u64 *result) {
    bool c = x <= 16u64;
    *result = x - 17u64;
    return c;
}
global bool flags_ugt64(u64 x, u64 *result) {
    bool c = x > 16u64;
    *result = x - 17u64;
    return c;
}
global bool result_zero64(u64 x, u64 *result) {
    const u64 y = x - 1u64;
    *result = y;
    return (i64)y == 0i64;
}
global bool result_nonzero64(u64 x, u64 *result) {
    const u64 y = x - 1u64;
    *result = y;
    return (i64)y != 0i64;
}
global bool result_negative64(u64 x, u64 *result) {
    const u64 y = x - 1u64;
    *result = y;
    return (i64)y < 0i64;
}
global bool result_positive64(u64 x, u64 *result) {
    const u64 y = x - 1u64;
    *result = y;
    return (i64)y > 0i64;
}
global bool result_nonnegative64(u64 x, u64 *result) {
    const u64 y = x - 1u64;
    *result = y;
    return (i64)y >= 0i64;
}
global bool result_nonpositive64(u64 x, u64 *result) {
    const u64 y = x - 1u64;
    *result = y;
    return (i64)y <= 0i64;
}
global u32 exit_count(i32 j, uptr i, i32 chain, i32 *result) {
    u32 n = 0;
    while (j >= 0 && i - (uptr)j <= 32768uptr && chain-- > 0) {
        n++;
        i++;
    }
    *result = chain;
    return n;
}

global bool exact_sle32(u32 x, u32 *result) {
    bool c = (i32)x <= 17i32;
    *result = x - 17u32;
    return c;
}

global bool exact_sgt32(u32 x, u32 *result) {
    bool c = (i32)x > 17i32;
    *result = x - 17u32;
    return c;
}

global bool exact_ule32(u32 x, u32 *result) {
    bool c = x <= 17u32;
    *result = x - 17u32;
    return c;
}

global bool exact_ugt32(u32 x, u32 *result) {
    bool c = x > 17u32;
    *result = x - 17u32;
    return c;
}

global bool logical_positive32(u32 x, u32 *result) {
    u32 v = x ^ 17u32;
    *result = v;
    return (i32)v > 0i32;
}

global bool exact_sle64(u64 x, u64 *result) {
    bool c = (i64)x <= 17i64;
    *result = x - 17u64;
    return c;
}

global bool exact_sgt64(u64 x, u64 *result) {
    bool c = (i64)x > 17i64;
    *result = x - 17u64;
    return c;
}

global bool exact_ule64(u64 x, u64 *result) {
    bool c = x <= 17u64;
    *result = x - 17u64;
    return c;
}

global bool exact_ugt64(u64 x, u64 *result) {
    bool c = x > 17u64;
    *result = x - 17u64;
    return c;
}

global bool logical_positive64(u64 x, u64 *result) {
    u64 v = x ^ 17u64;
    *result = v;
    return (i64)v > 0i64;
}

global u64 edge_live(u64 x, u32 pick, u64 a, u64 b) {
    u64 y = x;
    if (pick & 1u32) y = a;
    if (pick & 2u32) y += b;
    return y ^ x;
}

global bool carry32(u32 x, u32 *result) {
    bool c = x >= 1u32;
    *result = x - 1u32;
    return c;
}

// INT_MAX has no strict successor: `> INT_MAX` cannot become `>= INT_MAX + 1`
// with the flags of the subtraction of that constant.
global bool above_max32(u32 x, u32 *result) {
    *result = x - 2147483648u32;
    return (i32)x > 2147483647;
}

global bool mask_positive64(u64 x, u64 *result) {
    u64 v = x & 4294967295u64;
    *result = v;
    return (i64)v > 0i64;
}

global u64 phi_pressure(const u64 *a, u32 n) {
    u64 p0 = a[0], p1 = a[1], p2 = a[2], p3 = a[3], p4 = a[4], p5 = a[5], p6 = a[6], p7 = a[7], p8 = a[8], p9 = a[9], p10 = a[10], p11 = a[11], p12 = a[12], p13 = a[13], p14 = a[14], p15 = a[15];
    for (u32 k = 0; k < n; k++) {
        u64 t = p0;
        if (a[k] & 1u64) t = a[k + 1];
        if (a[k + 2] > t) t ^= p7;
        u64 old = p0;
        p0 = p1 ^ t;
        p1 = p2 + t;
        p2 = p3 ^ t;
        p3 = p4 + t;
        p4 = p5 ^ t;
        p5 = p6 + t;
        p6 = p7 ^ t;
        p7 = p8 + t;
        p8 = p9 ^ t;
        p9 = p10 + t;
        p10 = p11 ^ t;
        p11 = p12 + t;
        p12 = p13 ^ t;
        p13 = p14 + t;
        p14 = p15 ^ t;
        p15 = old + t;
    }
    return p0 ^ p1 ^ p2 ^ p3 ^ p4 ^ p5 ^ p6 ^ p7 ^ p8 ^ p9 ^ p10 ^ p11 ^ p12 ^ p13 ^ p14 ^ p15;
}

[[noinline]] global u64 scratch_use(u64 x) { return x * 3u64 + 7u64; }

global u64 phi_spilled_branch(u64 initial, const u64 *a, u64 *saved) {
    u64 p0 = initial, p1 = a[1], p2 = a[2], p3 = a[3], p4 = a[4], p5 = a[5], p6 = a[6], p7 = a[7], p8 = a[8], p9 = a[9], p10 = a[10], p11 = a[11], p12 = a[12], p13 = a[13], p14 = a[14], p15 = a[15];
    u64 r = p0;
    if (a[16] < a[17]) {
        r = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9 + p10 + p11 + p12 + p13 + p14 + p15;
        *saved = scratch_use(r + p0);
    }
    return r;
}

global bool add_zero32(u32 a, u32 b, u32 *result) {
    u32 sum = a + b;
    u32 v = sum + 1u32;
    *result = v;
    return v == 0u32;
}

global bool add_negative32(u32 a, u32 b, u32 *result) {
    u32 sum = a + b;
    u32 v = sum + 1u32;
    *result = v;
    return (i32)v < 0i32;
}

global bool add_zero64(u64 a, u64 b, u64 *result) {
    u64 sum = a + b;
    u64 v = sum + 1u64;
    *result = v;
    return v == 0u64;
}

global bool add_negative64(u64 a, u64 b, u64 *result) {
    u64 sum = a + b;
    u64 v = sum + 1u64;
    *result = v;
    return (i64)v < 0i64;
}
