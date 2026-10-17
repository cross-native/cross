// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Members of records in arrays whose stride an index scale cannot encode:
// a power-of-two stride shifts the index once and every member access reads
// base plus shifted index at its displacement. test_entry returns zero when
// every value matches; it runs with -fno-eval-calls, so that the calls run.

struct quad {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

struct wide {
    u64 a;
    u64 b;
    u64 c;
    u64 d;
};

struct trio {
    u64 a;
    u64 b;
    u64 c;
    u64 d;
    u64 e;
    u64 f;
};

static struct quad quads[32];
static struct wide wides[16];
static struct trio trios[8];

[[noinline]] global f32 quad_sum(in uptr k) {
    const uptr i = k & 31;
    return quads[i].x + quads[i].y + quads[i].z + quads[i].w;
}

[[noinline]] global void quad_set(in uptr k, in f32 v) {
    const uptr i = k & 31;
    quads[i].x = v;
    quads[i].w = v + 1.0f32;
}

[[noinline]] global u64 wide_mix(in uptr k) {
    const uptr i = k & 15;
    return wides[i].a ^ wides[i].d;
}

[[noinline]] global void wide_set(in uptr k, in u64 v) {
    const uptr i = k & 15;
    wides[i].b = v;
    wides[i].c = v * 3u64;
}

[[noinline]] global u64 trio_mix(in uptr k) {
    const uptr i = k & 7;
    return trios[i].b + trios[i].f;
}

global u32 test_entry() {
    for (uptr k = 0; k < 32; ++k) {
        quads[k].x = (f32)k;
        quads[k].y = (f32)(k * 2);
        quads[k].z = (f32)(k * 3);
        quads[k].w = 0.5f32;
    }
    for (uptr k = 0; k < 16; ++k) {
        wides[k].a = k * 11400714819323198485u64;
        wides[k].b = 0u64;
        wides[k].c = 0u64;
        wides[k].d = k + 7u64;
    }
    for (uptr k = 0; k < 8; ++k) {
        trios[k].b = k * 5u64;
        trios[k].f = k * 1000u64;
    }
    for (uptr k = 0; k < 64; ++k) {
        const uptr i = k & 31;
        if (quad_sum(k) != (f32)i + (f32)(i * 2) + (f32)(i * 3) + 0.5f32) {
            return 1u32;
        }
        const uptr j = k & 15;
        if (wide_mix(k) != ((j * 11400714819323198485u64) ^ (j + 7u64))) {
            return 2u32;
        }
        if (trio_mix(k) != (k & 7) * 1005u64) return 3u32;
    }
    quad_set(37, 4.0f32);
    if (quads[5].x != 4.0f32 || quads[5].y != 10.0f32 ||
        quads[5].z != 15.0f32 || quads[5].w != 5.0f32 ||
        quads[6].x != 6.0f32) {
        return 4u32;
    }
    wide_set(19, 9u64);
    if (wides[3].a != 3u64 * 11400714819323198485u64 || wides[3].b != 9u64 ||
        wides[3].c != 27u64 || wides[3].d != 10u64 || wides[4].b != 0u64) {
        return 5u32;
    }
    return 0u32;
}
