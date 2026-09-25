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
extern const std::uint8_t words[8];
extern const std::uint8_t float_bytes[4];
extern const std::uint8_t double_bytes[8];
extern const std::uint8_t quad_bytes[16];
extern const std::uint8_t extended_bytes[16];
extern const std::uint8_t pointer_scalars[16];
extern std::size_t extended_padding_ok;
extern std::ptrdiff_t meta_pointer_distance;
extern std::size_t meta_pointer_order_ok;
extern std::uint32_t source_word;
extern float float_value;
extern std::uint32_t endian_read_ok;
extern std::uint32_t endian_write_ok;
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
        signed_encoded[0] != 0xff || source_word != 0xff800041u ||
        endian_read_ok != 1 || endian_write_ok != 1 ||
        float_value != 1.5f || extended_padding_ok != 1 ||
        meta_pointer_distance != 2 || meta_pointer_order_ok != 1) return 1;
    constexpr std::uint8_t expected_words[] =
        {0x78, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a};
    for (std::size_t index = 0; index < 8; ++index)
        if (words[index] != expected_words[index]) return 5;
    constexpr std::uint8_t expected_float[] = {0x00, 0x00, 0xc0, 0x3f};
    constexpr std::uint8_t expected_double[] = {0, 0, 0, 0, 0, 0, 0, 0x80};
    for (std::size_t index = 0; index < 4; ++index)
        if (float_bytes[index] != expected_float[index]) return 6;
    for (std::size_t index = 0; index < 8; ++index)
        if (double_bytes[index] != expected_double[index]) return 7;
    for (std::size_t index = 0; index < 13; ++index)
        if (quad_bytes[index] != 0) return 8;
    if (quad_bytes[13] != 0x80 || quad_bytes[14] != 0xff ||
        quad_bytes[15] != 0x3f) return 9;
    for (std::size_t index = 0; index < 16; ++index) {
        const std::uint8_t expected = index == 7 ? 0xc0 :
            index == 8 ? 0xff : index == 9 ? 0x3f : 0;
        if (extended_bytes[index] != expected) return 10;
    }
    constexpr std::uint8_t expected_pointer_scalars[] = {
        0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0xf8, 0x3f};
    for (std::size_t index = 0; index < 16; ++index)
        if (pointer_scalars[index] != expected_pointer_scalars[index]) return 11;
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
