// An operation or conversion with a NaN operand produces at run time the NaN
// that translation-time evaluation produces: SSE, the x87 unit, and the
// binary128 sequences keep the NaN's sign and the leading bits of its
// payload, quieted. With two NaN operands, SSE subtraction and division and
// the binary128 sequences take the left one, and the x87 unit prefers a
// quiet NaN, then the larger significand, then the positive sign.

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };
union bits80 { f80 value; u64 words[2]; };
union bits128 { f128 value; u64 words[2]; };
static u32 b32(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 b64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }
static f32 f32_of(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }
static f64 f64_of(in u64 bits) { union bits64 cell; cell.bits = bits; return cell.value; }
static f80 f80_of(in u64 low, in u64 high) {
    union bits80 cell;
    cell.words[0] = low;
    cell.words[1] = high;
    return cell.value;
}
static f128 f128_of(in u64 low, in u64 high) {
    union bits128 cell;
    cell.words[0] = low;
    cell.words[1] = high;
    return cell.value;
}
static u64 low80(in f80 value) { union bits80 cell; cell.value = value; return cell.words[0]; }
static u64 high80(in f80 value) {
    union bits80 cell;
    cell.words[1] = 0u64;
    cell.value = value;
    return cell.words[1] & 0xffffu64;
}
static u64 low128(in f128 value) { union bits128 cell; cell.value = value; return cell.words[0]; }
static u64 high128(in f128 value) { union bits128 cell; cell.value = value; return cell.words[1]; }
static bool same80(in f80 a, in f80 b) { return low80(a) == low80(b) && high80(a) == high80(b); }
static bool same128(in f128 a, in f128 b) {
    return low128(a) == low128(b) && high128(a) == high128(b);
}

// Quiet (q) and signaling (s) NaNs of both signs with distinct payloads.
global volatile u32 q32 = 0x7fc00001u32;
global volatile u32 nq32 = 0xffc12345u32;
global volatile u32 s32 = 0xff800003u32;
global volatile u64 q64 = 0x7ff8000000000005u64;
global volatile u64 s64 = 0xfff0000000000006u64;
global volatile u64 s64_low = 0x7ff0000000000001u64;
global volatile u64 q80_low = 0xc000000000000001u64;
global volatile u64 q80_low2 = 0xc000000000000002u64;
global volatile u64 s80_low = 0xbfffffffffffffffu64;
global volatile u64 q128_high = 0x7fff800000000000u64;
global volatile u64 nq128_high = 0xffff812345678000u64;
global volatile u64 s128_high = 0x7fff000000000000u64;
global volatile f32 one32 = 1.0f32;
global volatile f64 one64 = 1.0;
global volatile f80 one80 = 1.0f80;
global volatile f128 one128 = 1.0f128;

global f32 sum32 = f32_of(0x7fc00001u32) + 1.0f32;
global f32 difference32 = 1.0f32 - f32_of(0xffc12345u32);
global f32 product32 = f32_of(0xff800003u32) * 1.0f32;
global f32 quotient32 = f32_of(0xffc12345u32) / f32_of(0x7fc00001u32);
global f32 signaling_quotient32 = f32_of(0x7fc00001u32) / f32_of(0xff800003u32);
global f32 root32 = $::sqrt(f32_of(0xff800003u32));
global f64 widened32 = (f64)f32_of(0xff800003u32);
global f64 sum64 = f64_of(0x7ff8000000000005u64) + 1.0;
global f64 difference64 = f64_of(0xfff0000000000006u64) - f64_of(0x7ff8000000000005u64);
global f32 narrowed64 = (f32)f64_of(0x7ff8000000000005u64);
global f32 narrowed_low64 = (f32)f64_of(0x7ff0000000000001u64);
global f64 mixed64 = f32_of(0xffc12345u32) + 1.0;

global f80 sum80 = f80_of(0xc000000000000001u64, 0x7fffu64) + f80_of(0xc000000000000002u64, 0xffffu64);
global f80 quiet_first80 = f80_of(0xbfffffffffffffffu64, 0x7fffu64) + f80_of(0xc000000000000001u64, 0x7fffu64);
global f80 signaling80 = f80_of(0xbfffffffffffffffu64, 0xffffu64) * f80_of(0x8000000000000005u64, 0x7fffu64);
global f80 tie80 = f80_of(0xc000000000000001u64, 0xffffu64) - f80_of(0xc000000000000001u64, 0x7fffu64);
global f80 root80 = $::sqrt(f80_of(0xbfffffffffffffffu64, 0xffffu64));
global f80 widened80 = (f80)f32_of(0xff800003u32);
global f32 narrowed80 = (f32)f80_of(0xc000010000000001u64, 0xffffu64);
global f64 narrowed80_64 = (f64)f80_of(0xbfffffffffffffffu64, 0x7fffu64);

global f128 sum128 = f128_of(1u64, 0x7fff800000000000u64) + 1.0f128;
global f128 difference128 = 1.0f128 - f128_of(0u64, 0xffff812345678000u64);
global f128 product128 = f128_of(7u64, 0x7fff000000000000u64) * 2.0f128;
global f128 quotient128 = f128_of(0u64, 0xffff812345678000u64) / f128_of(7u64, 0x7fff000000000000u64);
global f128 widened128 = (f128)f32_of(0xff800003u32);
global f128 widened64_128 = (f128)f64_of(0x7ff8000000000005u64);
global f128 widened80_128 = (f128)f80_of(0xbfffffffffffffffu64, 0xffffu64);
global f32 narrowed128 = (f32)f128_of(0u64, 0xffff812345678000u64);
global f64 narrowed128_64 = (f64)f128_of(0xf000000000000000u64, 0x7fff800000000001u64);
global f80 narrowed128_80 = (f80)f128_of(0xffff000000000000u64, 0x7fff000000000001u64);

global u32 test_entry() {
    const f32 q = f32_of(q32);
    const f32 nq = f32_of(nq32);
    const f32 s = f32_of(s32);
    if (b32(sum32) != b32(q + one32)) return 1u32;
    if (b32(difference32) != b32(one32 - nq)) return 2u32;
    if (b32(product32) != b32(s * one32)) return 3u32;
    if (b32(quotient32) != b32(nq / q)) return 4u32;
    if (b32(signaling_quotient32) != b32(q / s)) return 5u32;
    if (b32(root32) != b32($::sqrt(s))) return 6u32;
    if (b64(widened32) != b64((f64)s)) return 7u32;
    const f64 q_wide = f64_of(q64);
    const f64 s_wide = f64_of(s64);
    if (b64(sum64) != b64(q_wide + one64)) return 8u32;
    if (b64(difference64) != b64(s_wide - q_wide)) return 9u32;
    if (b32(narrowed64) != b32((f32)q_wide)) return 10u32;
    if (b32(narrowed_low64) != b32((f32)f64_of(s64_low))) return 11u32;
    if (b64(mixed64) != b64(nq + one64)) return 12u32;
    // The documented results, which the evaluated values must also be.
    if (b32(sum32) != 0x7fc00001u32 || b32(difference32) != 0xffc12345u32 ||
        b32(product32) != 0xffc00003u32 || b32(quotient32) != 0xffc12345u32 ||
        b32(signaling_quotient32) != 0x7fc00001u32 || b64(widened32) != 0xfff8000060000000u64 ||
        b32(narrowed_low64) != 0x7fc00000u32) return 13u32;

    const f80 q80 = f80_of(q80_low, 0x7fffu64);
    const f80 nq80 = f80_of(q80_low2, 0xffffu64);
    const f80 s80 = f80_of(s80_low, 0x7fffu64);
    const f80 ns80 = f80_of(s80_low, 0xffffu64);
    if (!same80(sum80, q80 + nq80)) return 20u32;
    if (!same80(quiet_first80, s80 + q80)) return 21u32;
    if (!same80(signaling80, ns80 * f80_of(0x8000000000000005u64, 0x7fffu64))) return 22u32;
    if (!same80(tie80, f80_of(q80_low, 0xffffu64) - q80)) return 23u32;
    if (!same80(root80, $::sqrt(ns80))) return 24u32;
    if (!same80(widened80, (f80)s)) return 25u32;
    if (b32(narrowed80) != b32((f32)f80_of(0xc000010000000001u64, 0xffffu64))) return 26u32;
    if (b64(narrowed80_64) != b64((f64)s80)) return 27u32;
    if (low80(sum80) != 0xc000000000000002u64 || high80(sum80) != 0xffffu64 ||
        low80(quiet_first80) != q80_low || high80(tie80) != 0x7fffu64 ||
        low80(signaling80) != 0xffffffffffffffffu64) return 28u32;

    const f128 q128 = f128_of(1u64, q128_high);
    const f128 nq128 = f128_of(0u64, nq128_high);
    const f128 s128 = f128_of(7u64, s128_high);
    if (!same128(sum128, q128 + one128)) return 30u32;
    if (!same128(difference128, one128 - nq128)) return 31u32;
    if (!same128(product128, s128 * 2.0f128)) return 32u32;
    if (!same128(quotient128, nq128 / s128)) return 33u32;
    if (!same128(widened128, (f128)s)) return 34u32;
    if (!same128(widened64_128, (f128)q_wide)) return 35u32;
    if (!same128(widened80_128, (f128)ns80)) return 36u32;
    if (b32(narrowed128) != b32((f32)nq128)) return 37u32;
    if (b64(narrowed128_64) != b64((f64)f128_of(0xf000000000000000u64, 0x7fff800000000001u64)))
        return 38u32;
    if (!same80(narrowed128_80, (f80)f128_of(0xffff000000000000u64, 0x7fff000000000001u64)))
        return 39u32;
    if (high128(difference128) != nq128_high || high128(product128) != 0x7fff800000000000u64 ||
        low128(product128) != 7u64) return 40u32;
    return 0u32;
}
