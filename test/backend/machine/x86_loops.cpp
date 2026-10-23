// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstdint>
#include <cstdio>
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
struct Bits { const unsigned char *src; std::uintptr_t len, pos; u32 buf; i32 cnt; };
struct Huff { std::int16_t count[16], sym[288]; };
extern "C" {
i32 chain_match(const unsigned char*, std::uintptr_t, std::uintptr_t, const i32*, const i32*, u32*);
i64 decode_all(Bits*, const Huff*, i32);
u32 phi_web(const u32*, u32);
i64 extend_signed(i32);
i64 extend_mask(i32);
i64 extend_signbit(i32);
u64 extend_unsigned(u32);
i64 extend_phi(u32, bool);
u32 exit_count(i32, std::uintptr_t, i32, i32*);
bool flags_eq32(u32, u32*);
bool flags_ne32(u32, u32*);
bool flags_slt32(u32, u32*);
bool flags_sge32(u32, u32*);
bool flags_sle32(u32, u32*);
bool flags_sgt32(u32, u32*);
bool flags_ult32(u32, u32*);
bool flags_uge32(u32, u32*);
bool flags_ule32(u32, u32*);
bool flags_ugt32(u32, u32*);
bool result_zero32(u32, u32*);
bool result_nonzero32(u32, u32*);
bool result_negative32(u32, u32*);
bool result_positive32(u32, u32*);
bool result_nonnegative32(u32, u32*);
bool result_nonpositive32(u32, u32*);
bool flags_eq64(u64, u64*);
bool flags_ne64(u64, u64*);
bool flags_slt64(u64, u64*);
bool flags_sge64(u64, u64*);
bool flags_sle64(u64, u64*);
bool flags_sgt64(u64, u64*);
bool flags_ult64(u64, u64*);
bool flags_uge64(u64, u64*);
bool flags_ule64(u64, u64*);
bool flags_ugt64(u64, u64*);
bool result_zero64(u64, u64*);
bool result_nonzero64(u64, u64*);
bool result_negative64(u64, u64*);
bool result_positive64(u64, u64*);
bool result_nonnegative64(u64, u64*);
bool result_nonpositive64(u64, u64*);
bool exact_sle32(u32, u32*);
bool exact_sgt32(u32, u32*);
bool exact_ule32(u32, u32*);
bool exact_ugt32(u32, u32*);
bool logical_positive32(u32, u32*);
bool exact_sle64(u64, u64*);
bool exact_sgt64(u64, u64*);
bool exact_ule64(u64, u64*);
bool exact_ugt64(u64, u64*);
bool logical_positive64(u64, u64*);
u64 edge_live(u64, u32, u64, u64);
bool carry32(u32, u32*);
bool above_max32(u32, u32*);
bool mask_positive64(u64, u64*);
bool add_zero32(u32, u32, u32*);
bool add_negative32(u32, u32, u32*);
bool add_zero64(u64, u64, u64*);
bool add_negative64(u64, u64, u64*);
u64 phi_pressure(const u64*, u32);
u64 phi_spilled_branch(u64, const u64*, u64*);
}
static u32 seed = 17;
static u32 random_word() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static int fail(const char* name) { std::fprintf(stderr, "%s failed\n", name); return 1; }
static u64 reference_pressure(const u64 *a, u32 n) {
    u64 p0 = a[0], p1 = a[1], p2 = a[2], p3 = a[3], p4 = a[4], p5 = a[5], p6 = a[6], p7 = a[7], p8 = a[8], p9 = a[9], p10 = a[10], p11 = a[11], p12 = a[12], p13 = a[13], p14 = a[14], p15 = a[15];
    for (u32 k = 0; k < n; k++) {
        u64 t = p0;
        if (a[k] & 1ull) t = a[k + 1];
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
static u64 reference_spilled_branch(u64 initial, const u64 *a, u64 *saved) {
    u64 p0 = initial, p1 = a[1], p2 = a[2], p3 = a[3], p4 = a[4], p5 = a[5], p6 = a[6], p7 = a[7], p8 = a[8], p9 = a[9], p10 = a[10], p11 = a[11], p12 = a[12], p13 = a[13], p14 = a[14], p15 = a[15];
    u64 r = p0;
    if (a[16] < a[17]) {
        r = p0 + p1 + p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9 + p10 + p11 + p12 + p13 + p14 + p15;
        *saved = (r + p0) * 3 + 7;
    }
    return r;
}
int main() {
    const u64 values[] = {0, 1, 16, 17, 18, 0x7fffffff, 0x80000000, 0xffffffff,
        0x7fffffffffffffff, 0x8000000000000000, 0xffffffffffffffff};
    for (u64 v : values) {
        u32 x32 = u32(v), out32;
        u64 x64 = v, out64;
        if (flags_eq32(x32, &out32) != (u32(x32) == 17) || out32 != x32 - 17) return fail("flags_eq32");
        if (flags_ne32(x32, &out32) != (u32(x32) != 17) || out32 != x32 - 17) return fail("flags_ne32");
        if (flags_slt32(x32, &out32) != (i32(x32) < 17) || out32 != x32 - 17) return fail("flags_slt32");
        if (flags_sge32(x32, &out32) != (i32(x32) >= 17) || out32 != x32 - 17) return fail("flags_sge32");
        if (flags_sle32(x32, &out32) != (i32(x32) <= 16) || out32 != x32 - 17) return fail("flags_sle32");
        if (flags_sgt32(x32, &out32) != (i32(x32) > 16) || out32 != x32 - 17) return fail("flags_sgt32");
        if (flags_ult32(x32, &out32) != (u32(x32) < 17) || out32 != x32 - 17) return fail("flags_ult32");
        if (flags_uge32(x32, &out32) != (u32(x32) >= 17) || out32 != x32 - 17) return fail("flags_uge32");
        if (flags_ule32(x32, &out32) != (u32(x32) <= 16) || out32 != x32 - 17) return fail("flags_ule32");
        if (flags_ugt32(x32, &out32) != (u32(x32) > 16) || out32 != x32 - 17) return fail("flags_ugt32");
        if (result_zero32(x32, &out32) != (i32(x32 - 1) == 0) || out32 != x32 - 1) return fail("result_zero32");
        if (result_nonzero32(x32, &out32) != (i32(x32 - 1) != 0) || out32 != x32 - 1) return fail("result_nonzero32");
        if (result_negative32(x32, &out32) != (i32(x32 - 1) < 0) || out32 != x32 - 1) return fail("result_negative32");
        if (result_positive32(x32, &out32) != (i32(x32 - 1) > 0) || out32 != x32 - 1) return fail("result_positive32");
        if (result_nonnegative32(x32, &out32) != (i32(x32 - 1) >= 0) || out32 != x32 - 1) return fail("result_nonnegative32");
        if (result_nonpositive32(x32, &out32) != (i32(x32 - 1) <= 0) || out32 != x32 - 1) return fail("result_nonpositive32");
        if (flags_eq64(x64, &out64) != (u64(x64) == 17) || out64 != x64 - 17) return fail("flags_eq64");
        if (flags_ne64(x64, &out64) != (u64(x64) != 17) || out64 != x64 - 17) return fail("flags_ne64");
        if (flags_slt64(x64, &out64) != (i64(x64) < 17) || out64 != x64 - 17) return fail("flags_slt64");
        if (flags_sge64(x64, &out64) != (i64(x64) >= 17) || out64 != x64 - 17) return fail("flags_sge64");
        if (flags_sle64(x64, &out64) != (i64(x64) <= 16) || out64 != x64 - 17) return fail("flags_sle64");
        if (flags_sgt64(x64, &out64) != (i64(x64) > 16) || out64 != x64 - 17) return fail("flags_sgt64");
        if (flags_ult64(x64, &out64) != (u64(x64) < 17) || out64 != x64 - 17) return fail("flags_ult64");
        if (flags_uge64(x64, &out64) != (u64(x64) >= 17) || out64 != x64 - 17) return fail("flags_uge64");
        if (flags_ule64(x64, &out64) != (u64(x64) <= 16) || out64 != x64 - 17) return fail("flags_ule64");
        if (flags_ugt64(x64, &out64) != (u64(x64) > 16) || out64 != x64 - 17) return fail("flags_ugt64");
        if (result_zero64(x64, &out64) != (i64(x64 - 1) == 0) || out64 != x64 - 1) return fail("result_zero64");
        if (result_nonzero64(x64, &out64) != (i64(x64 - 1) != 0) || out64 != x64 - 1) return fail("result_nonzero64");
        if (result_negative64(x64, &out64) != (i64(x64 - 1) < 0) || out64 != x64 - 1) return fail("result_negative64");
        if (result_positive64(x64, &out64) != (i64(x64 - 1) > 0) || out64 != x64 - 1) return fail("result_positive64");
        if (result_nonnegative64(x64, &out64) != (i64(x64 - 1) >= 0) || out64 != x64 - 1) return fail("result_nonnegative64");
        if (result_nonpositive64(x64, &out64) != (i64(x64 - 1) <= 0) || out64 != x64 - 1) return fail("result_nonpositive64");
        if (exact_sle32(x32, &out32) != (i32(x32) <= 17) || out32 != x32 - 17) return fail("exact_sle32");
        if (exact_sgt32(x32, &out32) != (i32(x32) > 17) || out32 != x32 - 17) return fail("exact_sgt32");
        if (exact_ule32(x32, &out32) != (u32(x32) <= 17) || out32 != x32 - 17) return fail("exact_ule32");
        if (exact_ugt32(x32, &out32) != (u32(x32) > 17) || out32 != x32 - 17) return fail("exact_ugt32");
        if (logical_positive32(x32, &out32) != (i32(x32 ^ 17) > 0) || out32 != (x32 ^ 17)) return fail("logical_positive32");
        if (exact_sle64(x64, &out64) != (i64(x64) <= 17) || out64 != x64 - 17) return fail("exact_sle64");
        if (exact_sgt64(x64, &out64) != (i64(x64) > 17) || out64 != x64 - 17) return fail("exact_sgt64");
        if (exact_ule64(x64, &out64) != (u64(x64) <= 17) || out64 != x64 - 17) return fail("exact_ule64");
        if (exact_ugt64(x64, &out64) != (u64(x64) > 17) || out64 != x64 - 17) return fail("exact_ugt64");
        if (logical_positive64(x64, &out64) != (i64(x64 ^ 17) > 0) || out64 != (x64 ^ 17)) return fail("logical_positive64");
        if (carry32(x32, &out32) != (x32 >= 1) || out32 != x32 - 1) return fail("carry32");
        if (mask_positive64(x64, &out64) != (i64(x64 & 0xffffffff) > 0) || out64 != (x64 & 0xffffffff)) return fail("mask_positive64");
        if (above_max32(x32, &out32) || out32 != x32 - 0x80000000u) return fail("above_max32");
        u64 dirty_argument = u64(x32) | 0xdeadbeef00000000ull, dirty_result;
        asm volatile("call extend_unsigned" : "=a"(dirty_result), "+D"(dirty_argument)
            : : "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11", "cc", "memory",
                "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
                "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
        if (dirty_result != x32) return fail("parameter upper bits");
        for (u64 other : values) {
            u32 expected_add32 = u32(x32 + u32(other) + 1);
            if (add_zero32(x32, u32(other), &out32) != (expected_add32 == 0) || out32 != expected_add32) return fail("add_zero32");
            if (add_negative32(x32, u32(other), &out32) != (i32(expected_add32) < 0) || out32 != expected_add32) return fail("add_negative32");
            u64 expected_add64 = u64(x64 + u64(other) + 1);
            if (add_zero64(x64, u64(other), &out64) != (expected_add64 == 0) || out64 != expected_add64) return fail("add_zero64");
            if (add_negative64(x64, u64(other), &out64) != (i64(expected_add64) < 0) || out64 != expected_add64) return fail("add_negative64");
        }
        if (extend_signed(i32(x32)) != i64(i32(x32)) ||
            extend_mask(i32(x32)) != i64(x32 & 32767) ||
            extend_signbit(i32(x32)) != i64(i32(x32 & 0x80000000)) ||
            extend_unsigned(x32) != u64(x32) ||
            extend_phi(x32, false) != i64(x32 & 32767) ||
            extend_phi(x32, true) != i64(x32 & 65535)) return fail("extensions");
    }
    for (int trial = 0; trial < 1500; ++trial) {
        std::array<unsigned char, 512> data{};
        std::array<i32, 32768> prev{};
        std::array<u32, 64> words{};
        for (auto& v : data) v = random_word() % (trial % 4 + 1);
        for (auto& v : words) v = random_word();
        for (unsigned k = 0; k < 128; ++k) prev[k] = k ? i32(k - 1 - random_word() % 3) : -1;
        i32 head = trial % 128;
        const std::uintptr_t i = 128, n = trial % 3 ? 512 : 130;
        const auto limit = n - i < 258 ? n - i : 258;
        std::uintptr_t best = 0;
        u32 dist = 0, actual = 0;
        int chain = 128;
        if (limit >= 3) for (i32 j = head; j >= 0 && i - std::uintptr_t(j) <= 32768 && chain-- > 0; j = prev[j & 32767]) {
            if (data[j + best] == data[i + best]) {
                std::uintptr_t l = 0;
                while (l < limit && data[i+l] == data[j+l]) ++l;
                if (l > best) { best = l; dist = u32(i-j); if (l == limit) break; }
            }
            if (prev[j & 32767] >= j) break;
        }
        const i32 expected = best >= 3 ? i32(best) : 0;
        if (chain_match(data.data(), n, i, &head, prev.data(), &actual) != expected || actual != dist) return fail("chain");
        u32 x = 3, y = 11, z = 7;
        const u32 count = trial % 65;
        for (u32 k = 0; k < count; ++k) {
            u32 t = words[k]; if (t & 1) t += x; if (t & 2) t ^= y;
            u32 old = x; x = y + t; y = z ^ old; z = t + old;
        }
        if (phi_web(words.data(), count) != (x ^ (y << 5) ^ (z >> 3))) return fail("phi web");
        const u64 original = (u64(random_word()) << 32) | random_word();
        const u64 aa = (u64(random_word()) << 32) | random_word();
        const u64 bb = (u64(random_word()) << 32) | random_word();
        u64 yy = trial & 1 ? aa : original;
        if (trial & 2) yy += bb;
        if (edge_live(original, trial, aa, bb) != (yy ^ original)) return fail("edge live");
        std::array<u64, 68> pressure{};
        for (auto& word : pressure) word = (u64(random_word()) << 32) | random_word();
        if (phi_pressure(pressure.data(), count) != reference_pressure(pressure.data(), count)) return fail("phi pressure");
        u64 actual_saved = 123, expected_saved = 123;
        const u64 expected_branch = reference_spilled_branch(original, pressure.data(), &expected_saved);
        if (phi_spilled_branch(original, pressure.data(), &actual_saved) != expected_branch || actual_saved != expected_saved) return fail("phi spilled branch");
        Huff h{}; h.count[3] = 8;
        for (int k = 0; k < 8; ++k) h.sym[k] = std::int16_t(k * 3);
        Bits b{data.data(), std::uintptr_t(trial % 64), 0, random_word() & 255, trial % 9}, ref = b;
        i64 sum = 0;
        const int symbols = trial % 170;
        for (int k = 0; k < symbols; ++k) {
            int code = 0; bool eof = false;
            for (int bit = 0; bit < 3; ++bit) {
                if (!ref.cnt) {
                    if (ref.pos >= ref.len) { eof = true; break; }
                    ref.buf = ref.src[ref.pos++]; ref.cnt = 8;
                }
                code = code * 2 + int(ref.buf & 1); ref.buf >>= 1; --ref.cnt;
            }
            if (eof) break;
            sum += code * 3;
        }
        if (decode_all(&b, &h, symbols) != sum || b.pos != ref.pos || b.buf != ref.buf || b.cnt != ref.cnt) return fail("decode");
        i32 old = trial % 140 - 5, a = 0, r = 0;
        std::uintptr_t pos = trial % 3 ? 128 : 40000;
        const i32 j = trial % 4 ? 127 : -1;
        u32 cnt = 0;
        while (j >= 0 && pos - std::uintptr_t(j) <= 32768 && old-- > 0) { ++cnt; ++pos; }
        r = old;
        if (exit_count(j, trial % 3 ? 128 : 40000, trial % 140 - 5, &a) != cnt || a != r) return fail("exit");
    }
    return 0;
}
