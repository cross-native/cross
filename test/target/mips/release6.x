// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// What Release 6 selects differently, against fixed results: multiply and
// divide of words and doublewords (word pairs without 64-bit registers),
// integer and floating selects, floating comparisons and the branches on
// them, the conversion to u32 that compares with 2^31, conversions of
// doubleword integers with 64-bit FPRs, minNum and maxNum with NaNs,
// misaligned accesses, LL/SC loops with multiply and divide updates,
// leading zeros and ones, calls with floating arguments, and a jump table.
// test_entry returns the number of wrong results; it runs with
// -fno-eval-calls.

union bits32 { f32 value; u32 bits; };
union bits64 { f64 value; u64 bits; };

static f32 f32_of(in u32 bits) { union bits32 cell; cell.bits = bits; return cell.value; }
static f64 f64_of(in u64 bits) { union bits64 cell; cell.bits = bits; return cell.value; }
static u32 bits_of32(in f32 value) { union bits32 cell; cell.value = value; return cell.bits; }
static u64 bits_of64(in f64 value) { union bits64 cell; cell.value = value; return cell.bits; }

[[noinline, runtime_only]] global u32 mul32(in u32 a, in u32 b) { return a * b; }
[[noinline, runtime_only]] global u32 mulhu32(in u32 a, in u32 b) {
    return (u32) (((u64) a * (u64) b) >> 32u32);
}
[[noinline, runtime_only]] global i32 mulhs32(in i32 a, in i32 b) {
    return (i32) (((i64) a * (i64) b) >> 32u32);
}
[[noinline, runtime_only]] global i32 div32(in i32 a, in i32 b) { return a / b; }
[[noinline, runtime_only]] global i32 rem32(in i32 a, in i32 b) { return a % b; }
[[noinline, runtime_only]] global u32 divu32(in u32 a, in u32 b) { return a / b; }
[[noinline, runtime_only]] global u32 remu32(in u32 a, in u32 b) { return a % b; }
// A quotient and remainder of the same operands.
[[noinline, runtime_only]] global u32 divmod32(in u32 a, in u32 b) {
    return (a / b) * 1000u32 + a % b;
}
[[noinline, runtime_only]] global u64 mul64(in u64 a, in u64 b) { return a * b; }
[[noinline, runtime_only]] global u64 mulhu64(in u64 a, in u64 b) {
    return (u64) (((u128) a * (u128) b) >> 64u32);
}
[[noinline, runtime_only]] global i64 mulhs64(in i64 a, in i64 b) {
    return (i64) (((i128) a * (i128) b) >> 64u32);
}
[[noinline, runtime_only]] global i64 div64(in i64 a, in i64 b) { return a / b; }
[[noinline, runtime_only]] global i64 rem64(in i64 a, in i64 b) { return a % b; }
[[noinline, runtime_only]] global u64 divu64(in u64 a, in u64 b) { return a / b; }
[[noinline, runtime_only]] global u64 remu64(in u64 a, in u64 b) { return a % b; }
// A scaled index that is no power of two multiplies the index.
[[noinline, runtime_only]] global u32 pick3(in u32 *table, in u32 index) {
    return table[index * 3u32];
}

[[noinline, runtime_only]] global u32 select32(in u32 c, in u32 a, in u32 b) {
    return c != 0u32 ? a : b;
}
[[noinline, runtime_only]] global i32 select_less(in i32 x, in i32 y, in i32 a, in i32 b) {
    return x < y ? a : b;
}
[[noinline, runtime_only]] global u64 select64(in u64 c, in u64 a, in u64 b) {
    return c == 0u64 ? a : b;
}
[[noinline, runtime_only]] global u32 maximum32(in u32 a, in u32 b) { return a > b ? a : b; }
[[noinline, runtime_only]] global f32 fselect_less(in f32 x, in f32 y, in f32 a, in f32 b) {
    return x < y ? a : b;
}
[[noinline, runtime_only]] global f64 fselect_ge(in f64 x, in f64 y, in f64 a, in f64 b) {
    return x >= y ? a : b;
}
[[noinline, runtime_only]] global f64 fselect_ne(in f64 x, in f64 y, in f64 a, in f64 b) {
    return x != y ? a : b;
}
[[noinline, runtime_only]] global f32 fselect_bool(in bool c, in f32 a, in f32 b) {
    return c ? a : b;
}
// The six predicates of two values as bits: <, <=, >, >=, ==, !=.
[[noinline, runtime_only]] global u32 compare32(in f32 a, in f32 b) {
    return (u32) (a < b) | (u32) (a <= b) << 1u32 | (u32) (a > b) << 2u32 |
           (u32) (a >= b) << 3u32 | (u32) (a == b) << 4u32 | (u32) (a != b) << 5u32;
}
[[noinline, runtime_only]] global u32 compare64(in f64 a, in f64 b) {
    return (u32) (a < b) | (u32) (a <= b) << 1u32 | (u32) (a > b) << 2u32 |
           (u32) (a >= b) << 3u32 | (u32) (a == b) << 4u32 | (u32) (a != b) << 5u32;
}
// Branches on comparisons.
[[noinline, runtime_only]] global u32 classify64(in f64 x) {
    if (x < 0.0) return 1u32;
    if (x == 0.0) return 2u32;
    if (x > 1.0) return 3u32;
    if (x <= 1.0) return 4u32;
    return 5u32;
}
[[noinline, runtime_only]] global u32 to_u32(in f64 x) { return (u32) x; }
[[noinline, runtime_only]] global u32 to_u32_single(in f32 x) { return (u32) x; }
#if $::has_feature($::feature::fp64)
// Doubleword integers convert in 64-bit FPRs, which word pairs reach
// through MTC1 and MTHC1 and leave through MFC1 and MFHC1.
[[noinline, runtime_only]] global f64 from_i64(in i64 x) { return (f64) x; }
[[noinline, runtime_only]] global f32 single_from_i64(in i64 x) { return (f32) x; }
[[noinline, runtime_only]] global i64 to_i64(in f64 x) { return (i64) x; }
[[noinline, runtime_only]] global i64 single_to_i64(in f32 x) { return (i64) x; }
#endif
[[noinline, runtime_only]] global f64 minimum64(in f64 a, in f64 b) { return $::fmin(a, b); }
[[noinline, runtime_only]] global f32 maximum_single(in f32 a, in f32 b) {
    return $::fmax(a, b);
}

struct record [[packed]] {
    u8 tag;
    u16 half;
    u32 word;
    u64 dword;
    f32 single;
    f64 double;
};
global struct record records[2];

[[noinline, runtime_only]] global u32 load_record(in struct record *r) {
    return (u32) r->half + r->word + (u32) (r->dword >> 32u32) + (u32) r->dword +
           (u32) r->single + (u32) r->double;
}
[[noinline, runtime_only]] global void store_record(in struct record *r, in u32 seed) {
    r->half = (u16) seed;
    r->word = seed * 3u32;
    r->dword = (u64) seed << 33u32 | 5u64;
    r->single = (f32) (seed & 0xffffu32);
    r->double = (f64) seed + 0.5;
}

global u32 [[atomic]] counter;

[[noinline, runtime_only]] global u32 atomics() {
    counter = 7u32;
    u32 wrong = 0u32;
    if ($::atomic_fetch_add(&counter, 5u32, $::memory::seq_cst) != 7u32) wrong = wrong + 1u32;
    if ($::atomic_exchange(&counter, 40u32, $::memory::seq_cst) != 12u32) wrong = wrong + 1u32;
    u32 expected = 40u32;
    if (!$::atomic_compare_exchange(&counter, &expected, 41u32, $::memory::seq_cst,
                                    $::memory::seq_cst)) {
        wrong = wrong + 1u32;
    }
    expected = 3u32;
    if ($::atomic_compare_exchange(&counter, &expected, 9u32, $::memory::seq_cst,
                                   $::memory::seq_cst) ||
        expected != 41u32) {
        wrong = wrong + 1u32;
    }
    if ((counter *= 3u32) != 123u32) wrong = wrong + 1u32;
    if ((counter /= 4u32) != 30u32) wrong = wrong + 1u32;
    if ((counter %= 7u32) != 2u32) wrong = wrong + 1u32;
    return wrong;
}

[[noinline, runtime_only]] global u32 leading32(in u32 x) { return $::clz(x) + 100u32 * $::clz(~x); }
[[noinline, runtime_only]] global u64 leading64(in u64 x) { return $::clz(x) + 100u64 * $::clz(~x); }

[[noinline, runtime_only]] global f64 combine(in f64 a, in i32 b, in f64 c, in f32 d, in f64 e,
                                               in u32 f, in f64 g) {
    return a * 2.0 + (f64) b + c * (f64) d - e + (f64) f * g;
}
[[noinline, runtime_only]] global f64 call_combine(in f64 x) {
    return combine(x, -3i32, 0.25, 8.0f32, 1.5, 7u32, x);
}
[[noinline, runtime_only]] static u32 twice(in u32 x) { return x * 2u32; }
[[noinline, runtime_only]] static u32 squared(in u32 x) { return x * x; }
[[noinline, runtime_only]] global u32 call_indirect(in u32 which, in u32 x) {
    u32 (*operations[2])(in u32) = {twice, squared};
    return operations[which & 1u32](x);
}
[[noinline, runtime_only]] global u32 dispatch(in u32 x) {
    switch (x) {
    case 0u32: return 11u32;
    case 1u32: return 23u32;
    case 2u32: return 37u32;
    case 3u32: return 41u32;
    case 4u32: return 59u32;
    case 5u32: return 67u32;
    case 6u32: return 73u32;
    default: return 0u32;
    }
}

global u32 test_entry() {
    u32 wrong = 0u32;
    if (mul32(0x12345u32, 0x6789u32) != 0x75cca2edu32) wrong = wrong + 1u32;
    if (mulhu32(0xdeadbeefu32, 0xfeedfaceu32) != 0xddbf6473u32) wrong = wrong + 1u32;
    if (mulhs32(-123456789i32, 987654321i32) != -28389653i32) wrong = wrong + 1u32;
    if (div32(-1000i32, 7i32) != -142i32 || rem32(-1000i32, 7i32) != -6i32) {
        wrong = wrong + 1u32;
    }
    if (divu32(0xfffffff0u32, 7u32) != 0x24924922u32 || remu32(0xfffffff0u32, 7u32) != 2u32) {
        wrong = wrong + 1u32;
    }
    if (divmod32(1234567u32, 1000u32) != 1234567u32) wrong = wrong + 1u32;
    if (mul64(0x0123456789abcdefu64, 0xfedcba9876543210u64) != 0x2236d88fe5618cf0u64) {
        wrong = wrong + 1u32;
    }
    if (mulhu64(0x0123456789abcdefu64, 0xfedcba9876543210u64) != 0x0121fa00ad77d742u64) {
        wrong = wrong + 1u32;
    }
    if (mulhs64(-0x0123456789abcdefi64, 0x76543210fedcba98i64) != -0x86a1c97652e6a9i64) {
        wrong = wrong + 1u32;
    }
    if (div64(-1000000000000i64, 7i64) != -142857142857i64 ||
        rem64(-1000000000000i64, 7i64) != -1i64) {
        wrong = wrong + 1u32;
    }
    if (divu64(0xfedcba9876543210u64, 1000u64) != 18364758544493064u64 ||
        remu64(0xfedcba9876543210u64, 1000u64) != 720u64) {
        wrong = wrong + 1u32;
    }
    u32 table[12] = {0u32, 1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32, 9u32, 10u32, 11u32};
    if (pick3(table, 3u32) != 9u32) wrong = wrong + 1u32;

    if (select32(5u32, 10u32, 20u32) != 10u32 || select32(0u32, 10u32, 20u32) != 20u32) {
        wrong = wrong + 1u32;
    }
    if (select_less(-5i32, 3i32, 1i32, 2i32) != 1i32 ||
        select_less(3i32, -5i32, 1i32, 2i32) != 2i32) {
        wrong = wrong + 1u32;
    }
    if (select64(0u64, 0x100000000u64, 7u64) != 0x100000000u64 ||
        select64(0x100000000u64, 0x100000000u64, 7u64) != 7u64) {
        wrong = wrong + 1u32;
    }
    if (maximum32(0x80000000u32, 5u32) != 0x80000000u32) wrong = wrong + 1u32;
    const f32 nan = f32_of(0x7fc00000u32);
    const f64 nan64 = f64_of(0x7ff8000000000000u64);
    if (fselect_less(1.0f32, 2.0f32, 3.5f32, 4.5f32) != 3.5f32 ||
        fselect_less(2.0f32, 1.0f32, 3.5f32, 4.5f32) != 4.5f32 ||
        fselect_less(nan, 1.0f32, 3.5f32, 4.5f32) != 4.5f32) {
        wrong = wrong + 1u32;
    }
    if (fselect_ge(2.0, 2.0, 3.5, 4.5) != 3.5 || fselect_ge(1.0, 2.0, 3.5, 4.5) != 4.5 ||
        fselect_ge(nan64, 2.0, 3.5, 4.5) != 4.5) {
        wrong = wrong + 1u32;
    }
    if (fselect_ne(1.0, 1.0, 3.5, 4.5) != 4.5 || fselect_ne(1.0, 2.0, 3.5, 4.5) != 3.5 ||
        fselect_ne(nan64, nan64, 3.5, 4.5) != 3.5) {
        wrong = wrong + 1u32;
    }
    if (fselect_bool((bool) 1u8, -1.25f32, 6.0f32) != -1.25f32 ||
        fselect_bool((bool) 0u8, -1.25f32, 6.0f32) != 6.0f32) {
        wrong = wrong + 1u32;
    }
    if (compare32(1.0f32, 2.0f32) != 0x23u32 || compare32(2.0f32, 2.0f32) != 0x1au32 ||
        compare32(3.0f32, 2.0f32) != 0x2cu32 || compare32(nan, 2.0f32) != 0x20u32) {
        wrong = wrong + 1u32;
    }
    if (compare64(-1.0, 2.0) != 0x23u32 || compare64(-0.0, 0.0) != 0x1au32 ||
        compare64(3.0, 2.0) != 0x2cu32 || compare64(2.0, nan64) != 0x20u32) {
        wrong = wrong + 1u32;
    }
    if (classify64(-2.0) != 1u32 || classify64(0.0) != 2u32 || classify64(7.0) != 3u32 ||
        classify64(0.5) != 4u32 || classify64(nan64) != 5u32) {
        wrong = wrong + 1u32;
    }
    if (to_u32(3000000000.75) != 3000000000u32 || to_u32(1.5) != 1u32 ||
        to_u32_single(4000000000.0f32) != 4000000000u32) {
        wrong = wrong + 1u32;
    }
#if $::has_feature($::feature::fp64)
    if (from_i64(-0x123456789abi64) != -1250999896491.0 ||
        from_i64(0x180000000i64) != 6442450944.0 ||
        single_from_i64(0x100000001i64) != 4294967296.0f32 ||
        to_i64(-1250999896491.75) != -0x123456789abi64 ||
        to_i64(6442450944.5) != 0x180000000i64 ||
        single_to_i64(-8589934592.0f32) != -0x200000000i64) {
        wrong = wrong + 1u32;
    }
#endif
    if (minimum64(nan64, 2.0) != 2.0 || minimum64(2.0, nan64) != 2.0 ||
        minimum64(-3.0, 2.0) != -3.0 || bits_of32(maximum_single(nan, 1.0f32)) != 0x3f800000u32 ||
        maximum_single(5.0f32, 1.0f32) != 5.0f32) {
        wrong = wrong + 1u32;
    }

    store_record(&records[1], 0x12345u32);
    if (records[1].half != 0x2345u16 || records[1].word != 0x369cfu32 ||
        records[1].dword != 0x2468a00000005u64 || records[1].single != 9029.0f32 ||
        records[1].double != 74565.5) {
        wrong = wrong + 1u32;
    }
    if (load_record(&records[1]) != 0x2345u32 + 0x369cfu32 + 0x2468au32 + 5u32 + 9029u32 +
                                        74565u32) {
        wrong = wrong + 1u32;
    }

    wrong = wrong + atomics();
    if (leading32(0x00f00000u32) != 8u32 || leading32(0xff0f0000u32) != 800u32) {
        wrong = wrong + 1u32;
    }
    if (leading64(0x0000000100000000u64) != 31u64 ||
        leading64(0xfffff00000000000u64) != 2000u64) {
        wrong = wrong + 1u32;
    }
    if (call_combine(10.0) != 20.0 - 3.0 + 2.0 - 1.5 + 70.0) wrong = wrong + 1u32;
    if (call_indirect(0u32, 21u32) != 42u32 || call_indirect(1u32, 12u32) != 144u32) {
        wrong = wrong + 1u32;
    }
    if (dispatch(4u32) != 59u32 || dispatch(6u32) != 73u32 || dispatch(9u32) != 0u32) {
        wrong = wrong + 1u32;
    }
    return wrong;
}
