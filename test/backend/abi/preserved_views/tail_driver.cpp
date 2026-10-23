// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>
using u64 = std::uint64_t;
extern "C" {
extern const char forward_priv[], entry[], forward[], m_forward[], allowed_tail[], loop_entry[];
extern void* priv_ptr;
u64 msabi_canary(const void*, u64, u64*);
}
int main() {
    __builtin_cpu_init();
    if (!__builtin_cpu_supports("avx2")) return 77;
    const void* functions[] = {forward_priv, priv_ptr, entry, forward, m_forward, allowed_tail, loop_entry};
    for (unsigned i = 0; i < 7; ++i) {
        u64 result = 0;
        const auto mask = msabi_canary(functions[i], 41, &result);
        if (mask || result != (i == 6 ? 861 : i == 2 ? 44 : 42)) {
            std::fprintf(stderr, "case %u: result %llu, lost-register mask %#llx\n",
                         i, (unsigned long long)result, (unsigned long long)mask);
            return 1;
        }
    }
}
