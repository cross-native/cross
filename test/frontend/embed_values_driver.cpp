// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>

extern "C" {
extern const std::uint8_t original[5];
extern const std::uint8_t copied[5];
extern std::uint8_t rotated[5];
extern std::uint8_t encoded[5];
extern std::uint8_t encoded_slice[3];
extern const std::uint8_t signed_encoded[1];
extern std::size_t asset_size;
extern std::size_t static_size;
extern std::size_t empty_capacity;
extern std::uint8_t first_byte;
extern std::uint8_t zero_byte;
extern std::uint8_t high_byte;
extern std::uint8_t second_byte;
extern std::int8_t signed_high;
extern std::int32_t signed_extended;
}

int main() {
    constexpr std::uint8_t input[] = {0x41, 0x00, 0x80, 0xff, 0x21};
    constexpr std::uint8_t output[] = {0x00, 0x80, 0xff, 0x21, 0x41};
    constexpr std::uint8_t inverted[] = {0xbe, 0xff, 0x7f, 0x00, 0xde};
    if (asset_size != 5 || static_size != 5 || empty_capacity != 0 ||
        first_byte != input[0] ||
        zero_byte != 0 || high_byte != 0xff || second_byte != 0 ||
        signed_high != -1 || signed_extended != -1 ||
        signed_encoded[0] != 0xff) return 1;
    for (std::size_t index = 0; index < 5; ++index) {
        if (original[index] != input[index] || copied[index] != input[index] ||
            rotated[index] != output[index] || encoded[index] != inverted[index])
            return 2;
    }
    for (std::size_t index = 0; index < 3; ++index)
        if (encoded_slice[index] != inverted[index + 1]) return 4;
    rotated[0] = 0;
    return original[0] == input[0] ? 0 : 3;
}
