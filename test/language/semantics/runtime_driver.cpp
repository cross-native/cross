// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <limits>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif
extern "C" {
CROSS_ABI std::uint32_t bool_integer(std::uint32_t);
CROSS_ABI std::uint32_t bool_float(double);
CROSS_ABI std::uint32_t bool_pointer(std::uint32_t*);
CROSS_ABI std::int64_t eval_widen(), eval_promoted(), eval_conditional();
CROSS_ABI std::int32_t eval_shift(), eval_escape(), eval_scope();
CROSS_ABI std::int32_t eval_mixed(), eval_unsigned_wrap(), eval_mul(), eval_div();
}
int main() {
    alignas(256) std::uint32_t object = 0;
    const bool ok = bool_integer(0) == 0 && bool_integer(2) == 1 &&
        bool_integer(256) == 1 && bool_float(0.5) == 1 &&
        bool_float(-0.5) == 1 && bool_float(-0.0) == 0 &&
        bool_float(std::numeric_limits<double>::quiet_NaN()) == 1 &&
        bool_pointer(&object) == 1 && eval_widen() == -1 &&
        eval_promoted() == -1 && eval_conditional() == 4294967295LL && eval_shift() == -2 &&
        eval_escape() == 10 && eval_scope() == 7 && eval_mixed() == 65537 &&
        static_cast<std::uint32_t>(eval_unsigned_wrap()) == 0xffffffffu &&
        eval_mul() == 90000 && eval_div() == -3;
    return ok ? 0 : 1;
}
