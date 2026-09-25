// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

extern "C" {
CROSS_ABI std::uint32_t grouped_tree();
CROSS_ABI std::uint32_t sliced_tree();
CROSS_ABI std::uint32_t joined_trees();
CROSS_ABI std::uint32_t empty_trees();
}

int main() {
    return grouped_tree() == 9 && sliced_tree() == 12 &&
           joined_trees() == 15 && empty_trees() == 1 ? 0 : 1;
}
