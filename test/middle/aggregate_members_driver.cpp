// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdio>
#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif
struct member_pair { std::uint32_t left, right; };
extern "C" CROSS_ABI std::uint32_t member_host_read(member_pair);
extern "C" CROSS_ABI int aggregate_members_entry();
int main() {
    const member_pair inputs[] = {{0, 0}, {7, 11}, {0xffffffffu, 0x87654321u}};
    for (auto pair : inputs) {
        if (member_host_read(pair) != pair.left + 3u * pair.right) return 1;
    }
    const auto result = aggregate_members_entry();
    if (result != 1) std::fprintf(stderr, "aggregate member case failed: %d\n", result);
    return result == 1 ? 0 : 2;
}
