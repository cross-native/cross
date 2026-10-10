// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>

#if defined(__GNUC__)
#define CROSS_MS_ABI __attribute__((ms_abi))
#else
#define CROSS_MS_ABI
#endif

extern CROSS_MS_ABI uint32_t test_entry(void);

int main(void) { return (int)test_entry(); }
