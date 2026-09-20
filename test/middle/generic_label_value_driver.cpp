// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

extern "C" CROSS_ABI std::uint64_t generic_label_value_entry();

int main() {
    return generic_label_value_entry() == 1 ? 0 : 1;
}
