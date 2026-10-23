// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Branches that the bounds of integers decide, on every path to them or on
// the path from one edge, compute the same results when they are folded or
// threaded: masks, shifts, remainders, conversions, signed and unsigned
// views, additions that may wrap, relations between two unknown values,
// phis whose incoming values decide a branch after their join, a loop whose
// first test the path into it decides, blocks with loads and stores copied
// onto a path, and loop phis that exchange values. test_entry returns zero
// when each checksum matches the one an independent C version of the
// functions computes, or the number of the first that differs. It runs with
// -fno-eval-calls, so that the calls run.

global u32 data[16];

[[noinline]] global u32 opaque(in u32 value) { return value; }

static u32 mix(in u32 sum, in u32 value) { return sum * 31u32 + value; }

// With 64 or more live slots, the pick (seed >> 8) & 63 is below the count,
// so its reduction loop is not entered, and the count is not zero.
[[noinline]] global u32 release(in u32 live, in u32 seed, in u32 flags) {
    u32 result = 0u32;
    if (live < 64u32 && (flags & 3u32) != 0u32) {
        result = live * 3u32;
    } else if (live != 0u32) {
        u32 pick = (seed >> 8) & 63u32;
        while (pick >= live) pick = pick - live;
        result = pick * 1000u32 + live;
    }
    return result;
}

[[noinline]] global u64 clamp_u64(in u64 value) {
    u64 x = value;
    if (x < 0x4000000000000000u64) x = 0x4000000000000000u64;
    if (x > 0xbfffffffffffffffu64) x = 0xbfffffffffffffffu64;
    return x;
}

[[noinline]] global i16 clamp_i16(in i16 value) {
    i16 x = value;
    if (x < -100i16) x = -100i16;
    if (x > 200i16) x = 200i16;
    return x;
}

// x + 1 and x - 1 may wrap, so they decide nothing beyond their bounds.
[[noinline]] global u32 wrapping(in u32 x) {
    u32 result = 0u32;
    if (x > 5u32) {
        if (x + 1u32 > 6u32) result = 1u32; else result = 2u32;
    }
    if (x < 3u32) {
        if (x - 1u32 < 2u32) result = result + 10u32; else result = result + 20u32;
    }
    return result;
}

[[noinline]] global u32 signs(in i32 x) {
    u32 result = 0u32;
    if (x < 0) {
        if ((u32)x >= 0x80000000u32) result = 1u32; else result = 2u32;
    } else {
        if ((u32)x < 0x80000000u32) result = 3u32; else result = 4u32;
    }
    if (x >= -3 && x <= 3) {
        if ((u32)(x + 3) <= 6u32) result = result + 10u32; else result = result + 20u32;
    }
    if (x > -10) {
        if (x >= -9) result = result + 100u32; else result = result + 200u32;
    }
    return result;
}

[[noinline]] global u32 bounded(in u32 x, in u32 y) {
    u32 result = 0u32;
    const u32 low = x & 15u32;
    const u32 high = x >> 28;
    const u32 rest = y % 10u32;
    if (low < 16u32) result = result + 1u32;
    if (high <= 15u32) result = result + 2u32;
    if (rest < 10u32) result = result + 4u32;
    if (low + high < 31u32) result = result + 8u32;
    if ((y | 1u32) != 0u32) result = result + 16u32;
    if ((low << 4) < 241u32) result = result + 32u32;
    if ((i32)(x | 0x80000000u32) < 0) result = result + 64u32;
    return result + low * 100u32 + high * 10000u32 + rest * 1000000u32;
}

// The value the phi takes from each arm decides the branch after the join.
[[noinline]] global u32 joined(in u32 c, in u32 a) {
    u32 t = 0u32;
    if (c != 0u32) t = 3u32; else t = (a & 7u32) | 8u32;
    if (t < 5u32) return t * 7u32 + a;
    return t * 11u32 + a;
}

[[noinline]] global u32 related(in u32 a, in u32 b) {
    u32 result = 0u32;
    if (a < b) {
        if (b > a) result = 1u32; else result = 2u32;
        if (a == b) result = result + 100u32;
        if (a <= b) result = result + 10u32;
    } else if (a == b) {
        if (a >= b && b >= a) result = 3u32;
    } else {
        if (b < a) result = 4u32; else result = 5u32;
    }
    return result;
}

[[noinline]] global i32 narrow(in i8 x, in u8 y) {
    i32 result = 0;
    if (x < 0i8) {
        const i32 w = (i32)x;
        if (w > -129) result = 1; else result = 2;
    }
    const u32 z = (u32)y;
    if (z < 256u32) result = result + 10;
    if ((u8)(y + 1u8) == 0u8) result = result + 100;
    if (x != -127i8 - 1i8) {
        if (x > -127i8 - 1i8) result = result + 1000;
    }
    return result;
}

// On the path from the mask, the loop's first test is decided.
[[noinline]] global u32 reduced(in u32 limit, in u32 x) {
    u32 k = x;
    if (limit >= 16u32) k = x & 15u32;
    while (k >= limit) k = k - limit;
    return k * 7u32 + limit;
}

// The join's load is copied onto the path whose constant decides its branch.
[[noinline]] global u32 loaded(in u32 i, in u32 flag) {
    u32 v = 0u32;
    const u32 index = i & 15u32;
    if (flag != 0u32) v = 5u32; else v = data[index];
    u32 w = data[(index + 1u32) & 15u32];
    if (v == 5u32) w = w + 1u32;
    data[index] = w;
    return w * 3u32 + v;
}

// So is the join's store, on both paths.
[[noinline]] global u32 stored(in u32 i, in u32 flag) {
    u32 v = 0u32;
    if (flag != 0u32) v = 7u32; else v = i & 3u32;
    data[i & 15u32] = v;
    if (v == 7u32) return data[(i + 1u32) & 15u32];
    return v;
}

[[noinline]] global u32 dominated(in u32 x) {
    if (x < 10u32) {
        const u32 y = x * 3u32;
        if (y > 30u32) return 99u32;
        return y;
    }
    return 1000u32;
}

// A value that a threaded path decides is read in a later loop, whose
// blocks with one predecessor lead back to its header.
struct Image { i32 npal; u8 pal[16][4]; u8 *ext; i32 next; };
global u8 extension[24];

static u32 be32(in const u8 *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

[[noinline]] global u32 cycled(in struct Image *img, in i32 w, in i32 h, in i32 bpp) {
    u32 sum = 0u32;
    const i32 has_ext = img->ext && img->next == 4 + img->npal;
    const u32 flags = has_ext ? be32(img->ext) : 0u32;
    for (i32 y = 0; y < h; y++) {
        for (i32 x = 0; x < w; x++) {
            if (bpp == 8) sum++;
        }
    }
    for (i32 i = 0; i < img->npal; i++) {
        sum += (u32)((img->pal[i][0] >> 3) << 11 |
                     (has_ext ? img->ext[4 + i] & 1 : img->pal[i][3] >= 128));
    }
    return sum + flags;
}

// The facts after a loop describe its last pass. Over the back edge, b
// takes a of the pass before, which is zero after one pass from x = 0
// while a is not.
[[noinline]] global u32 swapped(in u32 n, in u32 x) {
    u32 a = x;
    u32 b = 0x7fffffffu32;
    for (u32 k = 0u32; k < n; ++k) {
        const u32 t = b;
        b = a;
        a = t;
    }
    if (a >= 0x10000u32) {
        if (b != 0u32) return 1u32;
        return 2u32;
    }
    return 3u32;
}

global u32 test_entry() {
    const u32 lives[8] = {0u32, 1u32, 5u32, 63u32, 64u32, 65u32, 100u32, 4294967295u32};
    const u32 seeds[5] = {0u32, 0x3f00u32, 0x12345678u32, 0xffffffffu32, 0x2a00u32};
    const u32 flags[3] = {0u32, 1u32, 4u32};
    u32 sum = 0u32;
    for (u32 a = 0u32; a < opaque(8u32); ++a) {
        for (u32 b = 0u32; b < opaque(5u32); ++b) {
            for (u32 c = 0u32; c < opaque(3u32); ++c) {
                sum = mix(sum, $::runtime(release(lives[a], seeds[b], flags[c])));
            }
        }
    }
    if (sum != 23503553u32) return 1u32;
    const u64 wide[8] = {0u64, 1u64, 0x3fffffffffffffffu64, 0x4000000000000000u64,
                         0x8000000000000000u64, 0xbfffffffffffffffu64,
                         0xc000000000000000u64, 0xffffffffffffffffu64};
    u64 total = 0u64;
    for (u32 a = 0u32; a < opaque(8u32); ++a) {
        total = total * 31u64 + $::runtime(clamp_u64(wide[a]));
    }
    if (total != 4611686018427386911u64) return 2u32;
    const i16 halves[9] = {-32767i16 - 1i16, -101i16, -100i16, -99i16, 0i16, 199i16,
                           200i16, 201i16, 32767i16};
    i32 halves_sum = 0;
    for (u32 a = 0u32; a < opaque(9u32); ++a) {
        halves_sum = halves_sum * 3 + (i32)$::runtime(clamp_i16(halves[a]));
    }
    if (halves_sum != -963784) return 3u32;
    const u32 words[9] = {0u32, 1u32, 2u32, 3u32, 5u32, 6u32, 7u32, 4294967294u32,
                          4294967295u32};
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(9u32); ++a) sum = mix(sum, $::runtime(wrapping(words[a])));
    if (sum != 3038983573u32) return 4u32;
    const i32 ints[11] = {-2147483647 - 1, -11, -10, -9, -4, -3, -1, 0, 3, 4, 2147483647};
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(11u32); ++a) sum = mix(sum, $::runtime(signs(ints[a])));
    if (sum != 3151616097u32) return 5u32;
    const u32 bits[7] = {0u32, 15u32, 16u32, 0xf000000fu32, 0xffffffffu32, 0x7fffffffu32,
                         0x12345678u32};
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(7u32); ++a) {
        for (u32 b = 0u32; b < opaque(7u32); ++b) {
            sum = mix(sum, $::runtime(bounded(bits[a], bits[b])));
        }
    }
    if (sum != 2349264135u32) return 6u32;
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(7u32); ++a) {
        sum = mix(sum, $::runtime(joined(0u32, bits[a])));
        sum = mix(sum, $::runtime(joined(1u32, bits[a])));
    }
    if (sum != 340160631u32) return 7u32;
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(9u32); ++a) {
        for (u32 b = 0u32; b < opaque(9u32); ++b) {
            sum = mix(sum, $::runtime(related(words[a], words[b])));
        }
    }
    if (sum != 4264074463u32) return 8u32;
    const i8 bytes[6] = {-127i8 - 1i8, -127i8, -1i8, 0i8, 1i8, 127i8};
    const u8 ubytes[4] = {0u8, 1u8, 254u8, 255u8};
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(6u32); ++a) {
        for (u32 b = 0u32; b < opaque(4u32); ++b) {
            sum = sum * 3u32 + (u32)$::runtime(narrow(bytes[a], ubytes[b]));
        }
    }
    if (sum != 3364071216u32) return 9u32;
    const u32 limits[7] = {1u32, 3u32, 15u32, 16u32, 17u32, 64u32, 4294967295u32};
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(7u32); ++a) {
        for (u32 b = 0u32; b < opaque(7u32); ++b) {
            sum = mix(sum, $::runtime(reduced(limits[a], bits[b] & 0xffffu32)));
        }
    }
    if (sum != 450304812u32) return 10u32;
    for (u32 k = 0u32; k < opaque(16u32); ++k) data[k] = k * 5u32 + 1u32;
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(20u32); ++a) {
        sum = mix(sum, $::runtime(loaded(a * 3u32, a & 1u32)));
        sum = mix(sum, $::runtime(stored(a * 5u32, a % 3u32)));
    }
    for (u32 k = 0u32; k < opaque(16u32); ++k) sum = mix(sum, data[k]);
    if (sum != 2616888147u32) return 11u32;
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(9u32); ++a) sum = mix(sum, $::runtime(dominated(words[a])));
    sum = mix(sum, $::runtime(dominated(9u32)));
    sum = mix(sum, $::runtime(dominated(10u32)));
    if (sum != 949184249u32) return 12u32;
    for (u32 k = 0u32; k < opaque(24u32); ++k) extension[k] = (u8)(k * 37u32 + 5u32);
    struct Image img;
    sum = 0u32;
    for (i32 npal = 0; npal < (i32)opaque(16u32); npal += 5) {
        for (u32 e = 0u32; e < opaque(3u32); ++e) {
            for (i32 bpp = 4; bpp <= 8; bpp += 4) {
                img.npal = npal;
                for (u32 k = 0u32; k < 16u32; ++k) {
                    for (u32 c = 0u32; c < 4u32; ++c) img.pal[k][c] = (u8)(k * 29u32 + c * 71u32);
                }
                img.ext = e == 0u32 ? (u8 *)0 : &extension[0];
                img.next = e == 2u32 ? 4 + npal : 3;
                sum = mix(sum, $::runtime(cycled(&img, 3, 2, bpp)));
            }
        }
    }
    if (sum != 799702280u32) return 13u32;
    sum = 0u32;
    for (u32 a = 0u32; a < opaque(4u32); ++a) {
        for (u32 b = 0u32; b < opaque(9u32); ++b) {
            sum = mix(sum, $::runtime(swapped(a, words[b])));
        }
    }
    if (sum != 123637118u32) return 14u32;
    return 0u32;
}
