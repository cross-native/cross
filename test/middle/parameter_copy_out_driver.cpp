// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>
#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

// A C caller passes `out`/`inout` channels as pointers, and may pass the same
// pointer twice or one that the callee also reads through another parameter.
extern "C" CROSS_ABI void copy_out_accumulate(std::uint32_t, std::uint32_t *);
extern "C" CROSS_ABI std::uint32_t copy_out_alias(std::uint32_t *,
                                                  const std::uint32_t *,
                                                  std::uint32_t *);
extern "C" CROSS_ABI void copy_out_order(std::uint32_t *, std::uint32_t *);
extern "C" CROSS_ABI void copy_out_divide(std::uint32_t, std::uint32_t,
                                          std::uint32_t *, std::uint32_t *);
extern "C" CROSS_ABI std::uint32_t parameter_copy_out_entry();

static int check() {
    const auto entry = parameter_copy_out_entry();
    if (entry != 61) return static_cast<int>(entry) + 100;
    std::uint32_t total = 5;
    copy_out_accumulate(4, &total);
    if (total != 27) return 1;
    // The callee's cell is distinct until copy-out, so `view` sees 5.
    std::uint32_t shared = 5;
    std::uint32_t seen = 0;
    if (copy_out_alias(&shared, &shared, &seen) != 20) return 7;
    if (shared != 15 || seen != 5) return 2;
    shared = 5;
    if (copy_out_alias(&shared, &shared, &shared) != 20) return 8;
    if (shared != 5) return 3;
    std::uint32_t value = 0;
    copy_out_order(&value, &value);
    if (value != 2) return 4;
    std::uint32_t quotient = 0;
    std::uint32_t remainder = 0;
    copy_out_divide(47, 5, &quotient, &remainder);
    if (quotient != 9 || remainder != 2) return 5;
    copy_out_divide(47, 5, &quotient, &quotient);
    if (quotient != 2) return 6;
    return 0;
}

int main() {
    const auto result = check();
    if (result != 0) std::fprintf(stderr, "parameter copy-out case %d failed\n", result);
    return result;
}
