// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>

#if defined(__GNUC__)
#define CROSS_SYSV_ABI __attribute__((sysv_abi))
#else
#define CROSS_SYSV_ABI
#endif

extern CROSS_SYSV_ABI uint32_t unreferenced_entry(uint32_t value);
extern void *unreferenced_pointer;

int main(void) {
    if (unreferenced_entry(5) != 24) return 1;
    return unreferenced_pointer ? 0 : 2;
}
