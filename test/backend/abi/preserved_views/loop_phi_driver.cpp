// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
using u64 = std::uint64_t;
extern "C" {
extern u64 vin[4][4], vres[4][4];
void loop_phi(u64);
u64 ext(u64 x) { return x * 3 + 1; }
}
int main() {
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("avx2")) return 77;
    for (unsigned n = 0; n < 31; ++n) {
        for (unsigned j = 0; j < 4; ++j) { vin[0][j] = j * 19; vin[1][j] = 77 - j; }
        u64 a[4], b[4];
        for (unsigned j = 0; j < 4; ++j) { a[j] = vin[0][j]; b[j] = vin[1][j]; }
        for (unsigned i = 0; i < n; ++i) {
            for (unsigned j = 0; j < 4; ++j) { auto t = a[j]; a[j] = b[j] + ext(i); b[j] = t; }
        }
        loop_phi(n);
        for (unsigned j = 0; j < 4; ++j) {
            if (vres[0][j] != a[j] || vres[1][j] != b[j]) return 1;
        }
    }
}
