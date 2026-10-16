/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The partner of small_data_runtime.x compiled by GCC with -G8: it reaches
   that unit's small data through $gp, as -G8 assumes of a small external
   object, and defines a small and a large object. */
extern unsigned int counter;
extern unsigned int seed;
unsigned int partner_small = 41;
unsigned int partner_large[8] = {1, 2, 3, 4, 5, 6, 7, 8};

unsigned int partner_touch(unsigned int step)
{
    counter += step;
    return seed + partner_small + partner_large[2];
}
