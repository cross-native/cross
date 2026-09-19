// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

extern "C" std::uint32_t aligned_64();
extern "C" std::uint32_t aligned_128();
extern "C" std::uint32_t aligned_plain();

int main() {
    if ((reinterpret_cast<std::uintptr_t>(&aligned_64) & 63U) != 0) return 1;
    if ((reinterpret_cast<std::uintptr_t>(&aligned_128) & 127U) != 0) return 2;
    return aligned_64() == 64U && aligned_128() == 128U &&
                   aligned_plain() == 1U
               ? 0
               : 3;
}
