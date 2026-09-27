// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <cstdint>

extern "C" {
extern const std::uint8_t original[5];
extern const std::uint8_t copied[5];
extern const std::uint8_t parsed[5];
extern std::uint8_t rotated[5];
extern std::uint8_t encoded[5];
extern std::uint8_t encoded_slice[3];
extern const std::uint8_t signed_encoded[1];
extern const std::uint8_t words[8];
extern const std::uint8_t rows[8];
extern std::size_t array_view_checked;
extern std::size_t target_sized_array_checked;
extern std::size_t target_record_layout_checked;
extern std::size_t record_member_checked;
extern const std::uint8_t packed_record[9];
extern std::size_t packed_record_member_checked;
extern std::size_t nested_packed_record_checked;
extern const std::uint8_t union_bytes[4];
extern std::size_t union_member_checked;
extern std::size_t nested_union_member_checked;
extern std::size_t record_value_copy_checked;
extern std::size_t packed_record_value_checked;
extern std::size_t union_record_value_checked;
extern std::size_t bit_field_record_value_checked;
extern std::size_t nested_record_value_checked;
extern std::size_t meta_modifying_operators_checked;
extern const std::uint8_t bit_fields[2];
extern std::size_t bit_field_checked;
extern std::size_t record_array_member_checked;
extern std::size_t nested_record_member_checked;
extern std::size_t direct_nested_record_checked;
extern const std::uint8_t float_bytes[4];
extern const std::uint8_t double_bytes[8];
extern const std::uint8_t quad_bytes[16];
extern const std::uint8_t extended_bytes[16];
extern const std::uint8_t pointer_scalars[16];
extern std::size_t extended_padding_ok;
extern std::ptrdiff_t meta_pointer_distance;
extern std::size_t meta_pointer_order_ok;
extern std::uint32_t source_word;
extern std::uint32_t opaque_word;
extern std::size_t opaque_equal;
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
        opaque_word != 0xff800041u || opaque_equal != 1 ||
        endian_read_ok != 1 || endian_write_ok != 1 ||
        float_value != 1.5f || extended_padding_ok != 1 ||
        meta_pointer_distance != 2 || meta_pointer_order_ok != 1 ||
        array_view_checked != 1 || target_sized_array_checked != 1 ||
        target_record_layout_checked != 1 || record_member_checked != 1 ||
        packed_record_member_checked != 1 || nested_packed_record_checked != 1 ||
        union_member_checked != 1 || nested_union_member_checked != 1 ||
        record_value_copy_checked != 1 ||
        packed_record_value_checked != 1 || union_record_value_checked != 1 ||
        bit_field_record_value_checked != 1 ||
        nested_record_value_checked != 1 ||
        meta_modifying_operators_checked != 1 ||
        bit_field_checked != 1 ||
        record_array_member_checked != 1 || nested_record_member_checked != 1 ||
        direct_nested_record_checked != 1)
        return 1;
    constexpr std::uint8_t expected_words[] =
        {0x78, 0x56, 0x34, 0x12, 0xf0, 0xde, 0xbc, 0x9a};
    for (std::size_t index = 0; index < 8; ++index)
        if (words[index] != expected_words[index]) return 5;
    constexpr std::uint8_t expected_rows[] =
        {0x22, 0x11, 0x44, 0x33, 0x66, 0x55, 0x88, 0x77};
    for (std::size_t index = 0; index < 8; ++index)
        if (rows[index] != expected_rows[index]) return 12;
    constexpr std::uint8_t expected_packed_record[] =
        {0xa5, 0x78, 0x56, 0x34, 0x12, 0, 0, 0, 0};
    for (std::size_t index = 0; index < 9; ++index)
        if (packed_record[index] != expected_packed_record[index]) return 13;
    constexpr std::uint8_t expected_union_bytes[] = {0, 0, 0xc0, 0x3f};
    for (std::size_t index = 0; index < 4; ++index)
        if (union_bytes[index] != expected_union_bytes[index]) return 14;
    if (bit_fields[0] != 0x8d || bit_fields[1] != 0xab) return 15;
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
            parsed[index] != input[index] ||
            rotated[index] != output[index] || encoded[index] != inverted[index])
            return 2;
    }
    for (std::size_t index = 0; index < 3; ++index)
        if (encoded_slice[index] != inverted[index + 1]) return 4;
    rotated[0] = 0;
    return original[0] == input[0] ? 0 : 3;
}
