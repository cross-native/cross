// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>

#ifdef WIDE512
constexpr unsigned lanes = 8;
#else
constexpr unsigned lanes = 4;
#endif
extern "C" {
extern std::uint64_t vector_input[lanes], vector_output[lanes];
extern std::uint64_t narrow_input[2], narrow_output[2];
extern volatile std::uint64_t integer_input;
extern std::uint64_t integer_output;
extern volatile std::uint32_t small_input;
extern std::uint32_t small_output;
#define MS __attribute__((ms_abi))
void MS keep_wide();
void MS keep_narrow();
void MS call_private();
void MS use_preserved();
void use_custom();
void bridge_views();
void MS keep_custom_wide();
void MS keep_custom_integer();
void MS keep_custom_small();
void MS keep_explicit();
void MS keep_manual();
void MS keep_indirect();
void bridge_adapter();
std::uint64_t MS dynamic_custom_entry(std::uint64_t);
std::uint64_t MS gpr_entry64(std::uint64_t);
std::uint32_t MS gpr_entry32(std::uint32_t);
int check_ms(void (*)());
int check_custom(void (*)());
int check_strong(void (*)());
}

int main() {
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("avx2")) return 77;
#ifdef WIDE512
    if (!__builtin_cpu_supports("avx512f")) return 77;
#endif
    for (unsigned i = 0; i < lanes; ++i)
        vector_input[i] = 0x123456789abcdef0ULL + i * 0x111111111111111ULL;
    narrow_input[0] = 0x123456789abcdef0ULL;
    narrow_input[1] = 0xabcdef0198765432ULL;
    integer_input = 0x123456789abcdef0ULL;
    small_input = 0x76543210U;
    const auto wide_result = [](unsigned add) {
        for (unsigned i = 0; i < lanes; ++i) {
            if (vector_output[i] != vector_input[i] + add) return false;
        }
        return true;
    };
    const auto narrow_result = [] {
        return narrow_output[0] == narrow_input[0] + 3 &&
               narrow_output[1] == narrow_input[1] + 3;
    };
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "%s failed\n", #test); return 1; } } while (false)
    keep_wide();
    CHECK(wide_result(3));
    keep_narrow();
    CHECK(narrow_result());
    call_private();
    CHECK(wide_result(9));
    CHECK(check_ms(reinterpret_cast<void (*)()>(use_preserved)));

#ifndef MS_ONLY
    CHECK(check_custom(use_custom));
    CHECK(check_strong(bridge_views));
    CHECK(check_strong(bridge_adapter));
    CHECK(dynamic_custom_entry(5) == 22);
    CHECK(gpr_entry64(0x123456789abcdef0ULL) == 0x123456789abcdef0ULL);
    CHECK(gpr_entry32(0x76543210U) == 0x76543210U);
    keep_indirect();
    CHECK(wide_result(3));
    keep_custom_wide();
    CHECK(wide_result(3));
    keep_custom_integer();
    CHECK(integer_output == integer_input * 33);
    keep_custom_small();
    CHECK(small_output == std::uint32_t(small_input * 33));
    keep_explicit();
    CHECK(narrow_result());
    keep_manual();
    CHECK(narrow_result());
#endif
    return 0;
}
