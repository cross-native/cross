// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(__GNUC__)
#define CROSS_MS_ABI __attribute__((ms_abi))
#else
#define CROSS_MS_ABI
#endif

extern CROSS_MS_ABI unsigned gimple_vla_entry(unsigned);

int main(void) {
    // Each of 64 rounds adds (count - 1 + round) + 3.
    return gimple_vla_entry(4) == 2400 && gimple_vla_entry(9) == 2720 ? 0 : 1;
}
