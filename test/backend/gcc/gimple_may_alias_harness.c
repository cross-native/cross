// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdint.h>

#define CROSS_MS_ABI __attribute__((ms_abi))

extern CROSS_MS_ABI uint32_t gimple_may_alias_store(float *, uint32_t *);
extern CROSS_MS_ABI uint32_t gimple_may_alias_load(float *, uint32_t *);
extern CROSS_MS_ABI uint32_t gimple_may_alias_member(float *, uint32_t *);
extern CROSS_MS_ABI uint32_t gimple_may_alias_tag(float *, uint32_t *);

int main(void) {
    union { float f; uint32_t u; } cell = {0.0f};
    if (gimple_may_alias_store(&cell.f, &cell.u) != 0) return 1;
    if (gimple_may_alias_load(&cell.f, &cell.u) != UINT32_C(0x3f800000)) return 2;
    if (gimple_may_alias_member(&cell.f, &cell.u) != 0) return 3;
    if (gimple_may_alias_tag(&cell.f, &cell.u) != 0) return 4;
    return 0;
}
