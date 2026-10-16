// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// f80 and f128 operations on constants fold to the values that x87 code with
// a 64-bit significand and the inline binary128 sequences compute at run
// time. test_entry returns zero when every folded value matches. It runs
// with -fno-eval-calls, so that the optimizer folds the operations.

union bits80 { f80 value; u64 words[2]; };
union bits128 { f128 value; u64 words[2]; };

static u64 low80(in f80 value) { union bits80 cell; cell.value = value; return cell.words[0]; }
static u64 high80(in f80 value) {
    union bits80 cell;
    cell.words[1] = 0u64;
    cell.value = value;
    return cell.words[1] & 0xffffu64;
}
static u64 low128(in f128 value) { union bits128 cell; cell.value = value; return cell.words[0]; }
static u64 high128(in f128 value) { union bits128 cell; cell.value = value; return cell.words[1]; }

[[noinline]] global f80 add80(in f80 a, in f80 b) { return a + b; }
[[noinline]] global f80 div80(in f80 a, in f80 b) { return a / b; }
[[noinline]] global f80 root80(in f80 a) { return $::sqrt(a); }
[[noinline]] global f64 narrow80(in f80 a) { return (f64)a; }
[[noinline]] global f128 mul128(in f128 a, in f128 b) { return a * b; }
[[noinline]] global f128 div128(in f128 a, in f128 b) { return a / b; }
[[noinline]] global f80 sub80(in f80 a, in f80 b) { return a - b; }
[[noinline]] global f128 sub128(in f128 a, in f128 b) { return a - b; }
[[noinline]] global f128 widen128(in f80 a) { return (f128)a; }
[[noinline]] global f80 narrow128(in f128 a) { return (f80)a; }

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
static bool same80(in f80 a, in f80 b) { return low80(a) == low80(b) && high80(a) == high80(b); }
static bool same128(in f128 a, in f128 b) {
    return low128(a) == low128(b) && high128(a) == high128(b);
}

union bits64 { f64 value; u64 bits; };
static u64 b64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }

global u32 test_entry() {
    const f80 third = 1.0f80 / 3.0f80;
    const f80 runtime_third = $::runtime(div80(1.0f80, 3.0f80));
    if (low80(third) != low80(runtime_third) || high80(third) != high80(runtime_third))
        return 1u32;
    const f80 sum = 0.1f80 + 0.2f80;
    const f80 runtime_sum = $::runtime(add80(0.1f80, 0.2f80));
    if (low80(sum) != low80(runtime_sum) || high80(sum) != high80(runtime_sum))
        return 2u32;
    const f80 root = $::sqrt(2.0f80);
    const f80 runtime_root = $::runtime(root80(2.0f80));
    if (low80(root) != low80(runtime_root) || high80(root) != high80(runtime_root))
        return 3u32;
    if (b64((f64)(1.0f80 / 3.0f80)) != b64($::runtime(narrow80($::runtime(div80(1.0f80, 3.0f80))))))
        return 4u32;
    const f128 product = 1.1f128 * 3.3f128;
    const f128 runtime_product = $::runtime(mul128(1.1f128, 3.3f128));
    if (low128(product) != low128(runtime_product) ||
        high128(product) != high128(runtime_product)) return 5u32;
    const f128 quotient = 1.0f128 / 7.0f128;
    const f128 runtime_quotient = $::runtime(div128(1.0f128, 7.0f128));
    if (low128(quotient) != low128(runtime_quotient) ||
        high128(quotient) != high128(runtime_quotient)) return 6u32;
    // NaN operands keep their sign and payload, quieted.
    if (!same80(f80_of(0xbfffffffffffffffu64, 0xffffu64) - 1.0f80,
                $::runtime(sub80(f80_of(0xbfffffffffffffffu64, 0xffffu64), 1.0f80))))
        return 7u32;
    if (!same80($::sqrt(f80_of(0xc000000000000003u64, 0x7fffu64)),
                $::runtime(root80(f80_of(0xc000000000000003u64, 0x7fffu64)))))
        return 8u32;
    if (b64((f64)f80_of(0xc000000000000801u64, 0xffffu64)) !=
        b64($::runtime(narrow80(f80_of(0xc000000000000801u64, 0xffffu64))))) return 9u32;
    if (!same128(1.0f128 - f128_of(5u64, 0xffff800000000000u64),
                 $::runtime(sub128(1.0f128, f128_of(5u64, 0xffff800000000000u64)))))
        return 10u32;
    if (!same128(f128_of(9u64, 0x7fff000000000000u64) * 3.0f128,
                 $::runtime(mul128(f128_of(9u64, 0x7fff000000000000u64), 3.0f128))))
        return 11u32;
    if (!same128((f128)f80_of(0x8000000000000005u64, 0xffffu64),
                 $::runtime(widen128(f80_of(0x8000000000000005u64, 0xffffu64)))))
        return 12u32;
    if (!same80((f80)f128_of(0xffff000000000000u64, 0x7fff000000000001u64),
                $::runtime(narrow128(f128_of(0xffff000000000000u64, 0x7fff000000000001u64)))))
        return 13u32;
    // The optimizer folds an invalid operation and then an operation on its
    // negated NaN.
    if (!same80(-(0.0f80 / 0.0f80) - 1.0f80,
                $::runtime(sub80(-$::runtime(div80(0.0f80, 0.0f80)), 1.0f80))))
        return 15u32;
    if (!same128(-(0.0f128 / 0.0f128) - 1.0f128,
                 $::runtime(sub128(-$::runtime(div128(0.0f128, 0.0f128)), 1.0f128))))
        return 16u32;
    // An invalid binary128 operation produces the default NaN.
    if (!same128(0.0f128 * f128_of(0u64, 0x7fff000000000000u64),
                 $::runtime(mul128(0.0f128, f128_of(0u64, 0x7fff000000000000u64)))))
        return 14u32;
    return 0u32;
}
