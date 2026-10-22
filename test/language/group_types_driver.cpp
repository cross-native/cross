// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

struct figure {
    union { std::int32_t radius; std::int32_t side; };
    struct { std::int32_t x, y; } origin;
    std::uint8_t kind;
};

extern "C" CROSS_ABI std::int32_t group_one(figure *f);
extern "C" CROSS_ABI std::int32_t group_three(figure *items, std::uintptr_t count);

int main() {
    figure f[2] = {};
    f[0].radius = 2;
    f[0].kind = 1;
    f[1].side = 3;
    f[1].kind = 4;
    const bool ok = group_one(&f[0]) == 3 * 2 * 2 + 8 &&
        group_three(f, 2) == 12 + 9 + static_cast<std::int32_t>(sizeof(figure)) + 8;
    return ok ? 0 : 1;
}
