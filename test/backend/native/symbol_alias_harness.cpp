// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>

extern "C" std::uint32_t cross_alias_target_function();
extern "C" std::uint32_t cross_alias_function();
extern "C" std::uint32_t cross_alias_target_object;
extern "C" std::uint32_t cross_alias_object;

int main() {
    if (cross_alias_function() != 61U ||
        cross_alias_target_function() != 61U) {
        return 1;
    }
    if (cross_alias_object != 67U || cross_alias_target_object != 67U) {
        return 2;
    }
    return 0;
}
