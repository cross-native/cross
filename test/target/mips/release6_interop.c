/* Copyright (C) 2026 Cross contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/* The partner of release6_interop.x, compiled by GCC for Release 6: floating
   arguments and results, which the C ABI passes in 64-bit FPRs and on the
   stack, in both directions. */
double cross_scale(double x, float y, double z, int n, double w);
float cross_mix(float a, double b, float c, int d, float e);
unsigned cross_select(double a, double b, unsigned yes, unsigned no);

double c_scale(double x, float y, double z, int n, double w)
{
    return x * y + z * n - w;
}

float c_mix(float a, double b, float c, int d, float e)
{
    return a - (float)b * c + (float)d / e;
}

unsigned c_calls_cross(void)
{
    if (cross_scale(1.5, 2.0f, -0.25, 8, 3.0) != 1.5 * 2.0 - 0.25 * 8 - 3.0)
        return 1;
    if (cross_mix(10.0f, 0.5, 3.0f, 9, 2.0f) != 10.0f - 0.5f * 3.0f + 4.5f)
        return 2;
    if (cross_select(1.0, 2.0, 7u, 9u) != 7u || cross_select(2.0, 1.0, 7u, 9u) != 9u)
        return 3;
    return 0;
}
