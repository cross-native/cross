// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(__GNUC__)
#define CROSS_MS_ABI __attribute__((ms_abi))
#else
#define CROSS_MS_ABI
#endif

extern CROSS_MS_ABI unsigned gimple_aggregate_entry(unsigned);

int main(void) {
    return (int)gimple_aggregate_entry(2);
}
