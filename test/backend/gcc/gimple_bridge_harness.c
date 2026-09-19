// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define CROSS_MS_ABI __attribute__((ms_abi))
#else
#define CROSS_MS_ABI
#endif

extern CROSS_MS_ABI uint64_t gimple_bridge_sum(const uint64_t *, size_t);
extern CROSS_MS_ABI double gimple_bridge_divide(double, double);
extern CROSS_MS_ABI uint64_t gimple_bridge_select(const uint64_t *, size_t);
extern CROSS_MS_ABI uint64_t gimple_bridge_rotate(uint64_t, uint64_t);

struct gimple_patch_slot {
    uint32_t tag;
    uintptr_t cell;
};

extern struct gimple_patch_slot gimple_patch_record;

static uint64_t reference_select(const uint64_t *values, size_t count) {
    uint64_t total = 0;
    for (size_t index = 0; index < count; ++index) {
        const uint64_t value = values[index];
        total += value < UINT64_C(0x8000000000000000)
                     ? value * UINT64_C(5)
                     : value ^ UINT64_C(11400714819323198485);
    }
    return total;
}

int main(void) {
    const uint64_t values[] = {
        UINT64_C(0), UINT64_C(1), UINT64_C(0x7fffffffffffffff),
        UINT64_C(0x8000000000000000), UINT64_C(0xffffffffffffffff),
        UINT64_C(17), UINT64_C(0x9000000000000000), UINT64_C(91),
    };
    const size_t count = sizeof(values) / sizeof(values[0]);
    uint64_t sum = 0;
    for (size_t index = 0; index < count; ++index) sum += values[index];
    if (gimple_bridge_sum(values, count) != sum) return 1;
    if (gimple_bridge_divide(9.0, 4.0) != 2.25) return 2;
    if (gimple_bridge_select(values, count) !=
        reference_select(values, count)) {
        return 3;
    }
    const uint64_t rotate_value = UINT64_C(0x0123456789abcdef);
    const unsigned rotate_count = 13;
    const uint64_t rotated =
        (rotate_value << rotate_count) |
        (rotate_value >> (64 - rotate_count));
    if (gimple_bridge_rotate(rotate_value, rotate_count) != rotated) return 4;
    if (gimple_patch_record.tag != UINT32_C(77)) return 5;
    if (gimple_patch_record.cell == 0) return 6;
    return 0;
}
