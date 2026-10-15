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
    return 0u32;
}
