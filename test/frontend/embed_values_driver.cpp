// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>

extern "C" {
extern const std::uint8_t original[5];
extern std::uint8_t rotated[5];
extern std::size_t asset_size;
extern std::size_t static_size;
extern std::uint8_t first_byte;
extern std::uint8_t zero_byte;
extern std::uint8_t high_byte;
}

int main() {
    constexpr std::uint8_t input[] = {0x41, 0x00, 0x80, 0xff, 0x21};
    constexpr std::uint8_t output[] = {0x00, 0x80, 0xff, 0x21, 0x41};
    if (asset_size != 5 || static_size != 5 || first_byte != input[0] ||
        zero_byte != 0 || high_byte != 0xff) return 1;
    for (std::size_t index = 0; index < 5; ++index) {
        if (original[index] != input[index] || rotated[index] != output[index])
            return 2;
    }
    rotated[0] = 0;
    return original[0] == input[0] ? 0 : 3;
}
