/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The partner of abicalls_runtime.x compiled by GCC with -mabicalls and
   -ftls-model=initial-exec: global and thread-local objects, a function
   that calls back through a pointer, a tail call into that unit, and
   accesses to its thread-local object. */
unsigned int partner_value = 7;
unsigned int partner_table[4] = {10, 20, 30, 40};
__thread unsigned int partner_tls = 9;
extern __thread unsigned int tls_counter;
unsigned int twice(unsigned int x);

unsigned int partner_add(unsigned int a, unsigned int b) { return a + b + partner_value; }
unsigned int partner_apply(unsigned int (*f)(unsigned int), unsigned int x) { return f(x) + 1; }
unsigned int partner_twice(unsigned int x) { return twice(x); }

unsigned int partner_tls_bump(unsigned int step)
{
    tls_counter += step;
    partner_tls += step;
    return partner_tls;
}
