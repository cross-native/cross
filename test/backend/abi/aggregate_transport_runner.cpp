// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>

#if defined(__GNUC__) || defined(__clang__)
#define CROSS_SYSV __attribute__((sysv_abi))
#define CROSS_MS __attribute__((ms_abi))
#else
#define CROSS_SYSV
#define CROSS_MS
#endif

struct pair_i32 { std::int32_t left, right; };
struct pair_f32 { float left, right; };
struct pair_f64 { double left, right; };
struct mixed32 { float floating; std::int32_t integer; };
struct split_mixed { std::uint64_t integer; double floating; };
struct triple_i64 { std::int64_t first, second, third; };

#pragma pack(push, 1)
struct packed_i64 { std::uint8_t tag; std::uint64_t value; };
#pragma pack(pop)

struct three_bytes { std::uint8_t first, second, third; };
struct wrapped_f80 { long double value; };
struct bitfield_u32 { std::uint32_t ready : 1; std::uint32_t mode : 3; };
struct bitfield_mixed {
    std::uint32_t a : 3;
    std::int32_t b : 5;
    std::uint32_t c : 6;
};
union integer_or_float { std::uint64_t integer; double floating; };

extern "C" CROSS_SYSV pair_i32 cross_sysv_pair_i32(pair_i32);
extern "C" CROSS_SYSV pair_f32 cross_sysv_pair_f32(pair_f32);
extern "C" CROSS_SYSV pair_f64 cross_sysv_pair_f64(pair_f64);
extern "C" CROSS_SYSV mixed32 cross_sysv_mixed32(mixed32);
extern "C" CROSS_SYSV split_mixed cross_sysv_split_mixed(split_mixed);
extern "C" CROSS_SYSV triple_i64 cross_sysv_triple_i64(triple_i64);
extern "C" CROSS_SYSV packed_i64 cross_sysv_packed_i64(packed_i64);
extern "C" CROSS_SYSV wrapped_f80 cross_sysv_wrapped_f80(wrapped_f80);
extern "C" CROSS_SYSV integer_or_float cross_sysv_union(integer_or_float);
extern "C" CROSS_SYSV bitfield_u32 cross_sysv_bitfield_u32(bitfield_u32);
extern "C" CROSS_SYSV int cross_bitfield_mixed_size();
extern "C" CROSS_SYSV int cross_sysv_bitfield_mixed(bitfield_mixed);
extern "C" CROSS_MS pair_i32 cross_ms_pair_i32(pair_i32);
extern "C" CROSS_MS three_bytes cross_ms_three_bytes(three_bytes);
extern "C" CROSS_MS triple_i64 cross_ms_triple_i64(triple_i64);
extern "C" CROSS_MS bitfield_u32 cross_ms_bitfield_u32(bitfield_u32);
extern "C" CROSS_MS int cross_ms_bitfield_mixed(bitfield_mixed);
extern "C" int cross_aggregate_calls();

int main() {
    static_assert(sizeof(bitfield_u32) == 4);
    static_assert(sizeof(bitfield_mixed) == 4);
    const auto sysv_bitfields = cross_sysv_bitfield_u32({1, 3});
    const auto ms_bitfields = cross_ms_bitfield_u32({1, 3});
    const auto sysv_mixed_bits = cross_sysv_bitfield_mixed({5, -3, 33});
    const auto ms_mixed_bits = cross_ms_bitfield_mixed({5, -3, 33});
    const auto i32 = cross_sysv_pair_i32({10, 20});
    const auto f32 = cross_sysv_pair_f32({1.5f, 2.5f});
    const auto f64 = cross_sysv_pair_f64({3.5, 4.5});
    const auto mixed = cross_sysv_mixed32({5.5f, 6});
    const auto split = cross_sysv_split_mixed({7, 8.5});
    const auto large = cross_sysv_triple_i64({7, 8, 9});
    const auto packed = cross_sysv_packed_i64({10, 11});
    const auto f80 = cross_sysv_wrapped_f80({1.25L});
    integer_or_float union_input{};
    union_input.integer = 12;
    const auto union_value = cross_sysv_union(union_input);
    const auto ms_i32 = cross_ms_pair_i32({12, 13});
    const auto ms_odd = cross_ms_three_bytes({14, 15, 16});
    const auto ms_large = cross_ms_triple_i64({17, 18, 19});
    const auto internal_calls = cross_aggregate_calls();

    unsigned failures = 0;
    const auto check = [&](bool condition, const char* name) {
        if (!condition) {
            std::fprintf(stderr, "aggregate ABI mismatch: %s\n", name);
            ++failures;
        }
    };
    check(sysv_bitfields.ready == 0 && sysv_bitfields.mode == 5,
          "SysV u32 bit-field carrier");
    check(ms_bitfields.ready == 0 && ms_bitfields.mode == 5,
          "Win64 u32 bit-field carrier");
    check(sysv_mixed_bits == 1, "SysV mixed-sign bit-field carrier");
    check(ms_mixed_bits == 1, "Win64 mixed-sign bit-field carrier");
    check(cross_bitfield_mixed_size() == sizeof(bitfield_mixed),
          "mixed-sign bit-field size");
    check(i32.left == 11 && i32.right == 22, "SysV two i32");
    check(f32.left == 2.5f && f32.right == 4.5f, "SysV two f32");
    check(f64.left == 4.5 && f64.right == 6.5, "SysV two f64");
    check(mixed.floating == 6.5f && mixed.integer == 8, "SysV mixed");
    check(split.integer == 9 && split.floating == 11.5,
          "SysV split integer/SSE");
    check(large.first == 8 && large.second == 10 && large.third == 12,
          "SysV memory");
    check(packed.tag == 11 && packed.value == 13, "SysV packed");
    check(f80.value == 2.25L, "SysV x87 aggregate memory");
    check(union_value.integer == 16, "SysV union merge");
    check(ms_i32.left == 15 && ms_i32.right == 17, "Win64 eight-byte");
    check(ms_odd.first == 15 && ms_odd.second == 17 && ms_odd.third == 19,
          "Win64 odd-size indirect");
    check(ms_large.first == 21 && ms_large.second == 23 &&
              ms_large.third == 25,
          "Win64 large indirect");
    check(internal_calls == 12, "Cross call sites");
    return failures != 0;
}
