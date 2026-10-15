// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>
#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

extern "C" CROSS_ABI std::uint32_t inlining_runtime_entry();

int main() {
    const auto result = inlining_runtime_entry();
    if (result != 77) {
        std::fprintf(stderr, "inlining runtime case %u failed\n",
                     static_cast<unsigned>(result));
        return 1;
    }
    return 0;
}
