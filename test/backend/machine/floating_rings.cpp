// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>
#include <cstring>

using u32 = std::uint32_t;

extern "C" {
extern float ring_f32[64];
extern double ring_f64[64];
float ring14(u32);
double ring15(u32);
float branches(u32);
double nested(u32);
float early_exit(u32);
double calls(u32);
float lanes(u32);
float swap4(u32);
float swap8(u32);
double tail(u32);
float wide(u32);
}

// One IEEE operation per statement, so no host contraction applies.
template <typename T, int N>
static T sum(const T (&v)[N]) {
    T total = v[0];
    for (int k = 1; k < N; ++k) total = total + v[k];
    return total;
}

static float ref_ring14(u32 count) {
    float v[14];
    for (int k = 0; k < 14; ++k) v[k] = ring_f32[k];
    for (u32 i = 0; i < count; ++i) {
        for (int k = 0; k < 13; ++k) v[k] = v[k] + v[k + 1];
        v[13] = v[13] + v[0];
    }
    return sum(v);
}

static double ref_ring15(u32 count) {
    double v[15];
    for (int k = 0; k < 15; ++k) v[k] = ring_f64[k];
    for (u32 i = 0; i < count; ++i) {
        for (int k = 0; k < 14; ++k) {
            const double t = v[k + 1] * 0.5;
            v[k] = v[k] - t;
        }
        const double t = v[14] * 0.75;
        v[14] = t + v[0];
    }
    return sum(v);
}

static float ref_branches(u32 count) {
    float v[16];
    for (int k = 0; k < 16; ++k) v[k] = ring_f32[k + 16];
    for (u32 i = 0; i < count; ++i) {
        if ((i & 1) == 0) {
            v[0] = v[0] + v[15];
            v[5] = v[5] * 0.5f;
            v[9] = v[9] - v[2];
        } else {
            v[1] = v[1] - v[0];
            v[7] = v[7] + v[12];
        }
        for (int k = 0; k < 15; ++k) {
            const float t = v[k + 1] * 0.25f;
            v[k] = v[k] + t;
        }
        const float t = v[0] * 0.25f;
        v[15] = v[15] - t;
    }
    return sum(v);
}

static double ref_nested(u32 count) {
    double v[14];
    for (int k = 0; k < 14; ++k) v[k] = ring_f64[k + 20];
    for (u32 i = 0; i < count; ++i) {
        for (int j = 0; j < 3; ++j) {
            const double t = v[0] * 0.5;
            v[0] = t + v[13];
            v[6] = v[6] - v[1];
        }
        for (int k = 0; k < 13; ++k) v[k] = v[k] + v[k + 1];
        const double t = v[13] * 0.25;
        v[13] = t - v[0];
    }
    return sum(v);
}

static float ref_early_exit(u32 count) {
    float v[15];
    for (int k = 0; k < 15; ++k) v[k] = ring_f32[k + 33];
    for (u32 i = 0; i < count; ++i) {
        for (int k = 0; k < 8; ++k) v[k] = v[k] + v[k + 1];
        if (v[3] > 100.0f) break;
        for (int k = 8; k < 14; ++k) v[k] = v[k] + v[k + 1];
        v[14] = v[14] + v[0];
    }
    return sum(v);
}

static double ref_bump(double x) {
    const double t = x * 0.5;
    return t + 1.0;
}

static double ref_calls(u32 count) {
    double v[14];
    for (int k = 0; k < 14; ++k) v[k] = ring_f64[k + 40];
    for (u32 i = 0; i < count; ++i) {
        for (int k = 0; k < 7; ++k) v[k] = v[k] + v[k + 1];
        v[4] = ref_bump(v[4]);
        for (int k = 7; k < 13; ++k) v[k] = v[k] + v[k + 1];
        v[13] = v[13] + v[0];
    }
    return sum(v);
}

static float ref_lanes(u32 count) {
    float a[4] = {ring_f32[0], ring_f32[7], ring_f32[14], ring_f32[21]};
    float b[4] = {ring_f32[3], ring_f32[10], ring_f32[17], ring_f32[24]};
    for (u32 i = 0; i < count; ++i) {
        for (int l = 0; l < 4; ++l) {
            if ((i & 3) == 0) {
                const float s = a[l] + b[l];
                b[l] = s * 0.125f;
                a[l] = a[l] + b[l];
                const float t = a[l] * 2.0f;
                a[l] = t + b[l];
            } else {
                const float s = a[l] + b[l];
                a[l] = s * 2.0f;
                b[l] = b[l] + a[l];
            }
            const float t = a[l] * 0.25f;
            a[l] = t + b[l];
            b[l] = b[l] - a[l];
            if ((i & 3) == 0) {
                const float s = a[l] + b[l];
                a[l] = s * 0.25f;
                b[l] = b[l] - a[l];
            } else {
                b[l] = b[l] + a[l];
            }
            const float u = b[l] * -0.25f;
            b[l] = u + a[l];
        }
    }
    const float v[8] = {a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]};
    return sum(v);
}

// A swap leaves the vectors exchanged after an odd count.
static float ref_swap(u32 count, float a0, float a2, float b) {
    const bool odd = (count & 1) != 0;
    const float first = odd ? b : a0;
    const float second = odd ? b : a2;
    const float third = odd ? a0 : b;
    const float t = first * 2.0f;
    const float u = second * 4.0f;
    const float s = t + u;
    return s + third;
}

static double ref_tail(u32 count) {
    double v[12][2];
    for (int k = 0; k < 12; ++k) {
        v[k][0] = ring_f64[2 * k];
        v[k][1] = ring_f64[2 * k + 1];
    }
    for (u32 i = 0; i < count; ++i) {
        for (int l = 0; l < 2; ++l) {
            for (int k = 0; k < 11; ++k) {
                const double t = v[k + 1][l] * 0.25;
                v[k][l] = v[k][l] - t;
            }
            const double t = v[11][l] * 0.5;
            v[11][l] = t + v[0][l];
        }
    }
    double total = v[0][0];
    for (int k = 0; k < 12; ++k) {
        for (int l = k == 0 ? 1 : 0; l < 2; ++l) total = total + v[k][l];
    }
    total = total + ring_f64[40];
    return total + ring_f64[41];
}

static float ref_wide() {
    float total = ring_f32[0];
    for (int k = 0; k < 9; ++k) {
        for (int l = k == 0 ? 1 : 0; l < 8; ++l) {
            total = total + ring_f32[(3 * k + 7 * l) % 64];
        }
    }
    return total;
}

template <typename T>
static bool same(T actual, T expected) {
    return std::memcmp(&actual, &expected, sizeof actual) == 0;
}

int main() {
    for (int i = 0; i < 64; ++i) {
        ring_f32[i] = float((i * 37) % 23) * 0.375f - 2.0f;
        ring_f64[i] = double((i * 41) % 29) * 0.3125 - 3.0;
    }
    int failures = 0;
    const auto check = [&](const char* name, u32 count, bool equal) {
        if (equal) return;
        std::printf("%s(%u) differs\n", name, count);
        ++failures;
    };
    for (u32 count = 0; count < 12; ++count) {
        check("ring14", count, same(ring14(count), ref_ring14(count)));
        check("ring15", count, same(ring15(count), ref_ring15(count)));
        check("branches", count, same(branches(count), ref_branches(count)));
        check("nested", count, same(nested(count), ref_nested(count)));
        check("early_exit", count,
              same(early_exit(count), ref_early_exit(count)));
        check("calls", count, same(calls(count), ref_calls(count)));
        check("lanes", count, same(lanes(count), ref_lanes(count)));
        check("swap4", count,
              same(swap4(count),
                   ref_swap(count, ring_f32[1], ring_f32[2], ring_f32[3])));
        check("swap8", count,
              same(swap8(count),
                   ref_swap(count, ring_f32[4], ring_f32[5], ring_f32[6])));
        check("tail", count, same(tail(count), ref_tail(count)));
        check("wide", count, same(wide(count), ref_wide()));
    }
    return failures != 0;
}
