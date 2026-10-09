// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(__GNUC__)
#define CROSS_MS_ABI __attribute__((ms_abi))
#else
#define CROSS_MS_ABI
#endif

extern CROSS_MS_ABI unsigned gimple_indirect_entry(unsigned);

int main(void) {
    // twice(5) + twice(1) + square(3) + 100 and square(4) + 2 + 9 + 100.
    return gimple_indirect_entry(5) == 121 && gimple_indirect_entry(4) == 127
               ? 0
               : 1;
}
