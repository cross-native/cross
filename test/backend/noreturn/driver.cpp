// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>
#include <cstdlib>

extern "C" [[noreturn]] void __attribute__((ms_abi)) never(
    std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t);
extern "C" void __attribute__((ms_abi)) sink(std::uint64_t value) {
    static unsigned calls;
    if (value != 0x123456789abcdef0ULL) std::exit(1);
    if (++calls == 64) std::exit(0);
}
int main() { never(0x123456789abcde00ULL, 1, 2, 3, 0xf0); }
