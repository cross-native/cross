typedef i16 i16x8 [[ext_vector_type(8)]];
typedef i32 i32x8 [[ext_vector_type(8)]];
typedef u32 u32x8 [[ext_vector_type(8)]];
typedef u16 u16x16 [[vector_size(32)]];
typedef i64 i64x8 [[ext_vector_type(8)]];
typedef f32 f32x8 [[ext_vector_type(8)]];
typedef f64 f64x8 [[ext_vector_type(8)]];

global i32x8 wide_cell;
global i32x8 wide_cells[2];
global i64x8 huge_cell;
global volatile i32 wide_lane;

[[abi("sysv_abi"), noinline]]
static i32x8 wide_sysv(in i32x8 value) {
    return value + 1;
}

[[abi("ms_abi"), noinline]]
static i32x8 wide_ms(in i32x8 value) {
    return value + 2;
}

[[abi("sysv_abi"), noinline]]
static i64x8 huge_sysv(in i64x8 value) {
    return value + 3;
}

[[abi("ms_abi"), noinline]]
static i64x8 huge_ms(in i64x8 value) {
    return value + 4;
}

[[abi("sysv_abi"), noinline]]
static void wide_sysv_inout(inout i32x8 value) {
    value += 5;
}

[[abi("ms_abi"), noinline]]
static void huge_ms_out(out i64x8 value) {
    value = 15;
}

global i32 wide_vector_entry() {
    i32x8 left = 3;
    i32x8 right = 2;
    i32x8 sum = left + right;
    i32x8 product = sum * right;
    i32x8 quotient = product / right;
    i32x8 remainder = product % 3;
    i32x8 negative = -7;
    i32x8 signed_quotient = negative / 3;
    i32x8 signed_remainder = negative % 3;
    i32x8 anded = left & right;
    i32x8 ored = left | right;
    i32x8 xored = left ^ right;
    i32x8 negated = -left;
    i32x8 inverted = ~left;
    i32x8 zero_mask = !(left - left);
    i32x8 nonzero_mask = !left;
    i32x8 mask = sum > right;
    i32x8 equal_mask = sum == 5;
    u32x8 high = 4294967295u32;
    u32x8 low = 1u32;
    i32x8 unsigned_mask = high > low;
    u32x8 retyped = sum;

    u16x16 bits = 3u16;
    u16x16 shifted = bits << 2;
    u16x16 unshifted = shifted >> 2;

    f32x8 first = 1.5f32;
    f32x8 second = 0.5f32;
    f32x8 floats = (first + second) * 2.0f32;
    f64x8 doubles = 3.0f64 / 2.0f64;
    f64x8 negative_doubles = -doubles;
    i64x8 double_mask = doubles > 1.0f64;

    i64x8 huge = 9;
    i64x8 huge_sum = huge + 4;
    i64x8 huge_zero_mask = !(huge - huge);
    i32x8 called_sysv = wide_sysv(sum);
    i32x8 called_ms = wide_ms(sum);
    i64x8 called_huge_sysv = huge_sysv(huge);
    i64x8 called_huge_ms = huge_ms(huge);
    i32x8 changed = 8;
    wide_sysv_inout(changed);
    i64x8 produced = 0;
    huge_ms_out(produced);

    i16x8 narrow = -4;
    i32x8 widened = narrow;
    i16x8 narrowed = widened + 1;
    f64x8 extended = floats;
    f32x8 truncated = extended;

    wide_cell = sum;
    i32x8 *cell = &wide_cell;
    i32x8 transported = cell[0];
    wide_lane = 6;
    transported[wide_lane] = 9;
    *cell = transported;

    wide_cells[0] = sum;
    wide_cells[1] = product;
    i32x8 indexed = wide_cells[1];

    huge_cell = huge_sum;
    i64x8 *huge_pointer = &huge_cell;
    i64x8 huge_loaded = huge_pointer[0];

    i32x8 selected;
    if (transported[wide_lane] == 9) {
        selected = sum;
    } else {
        selected = product;
    }

    i32 scalar = quotient[0] + quotient[1] + quotient[2] + quotient[3] +
                 quotient[4] + quotient[5] + quotient[6] + quotient[7];
    return scalar + (mask[0] == -1) + (equal_mask[7] == -1) +
           (unsigned_mask[3] == -1) + (shifted[15] == 12u16) +
           (unshifted[14] == 3u16) + (floats[3] == 4.0f32) +
           (doubles[6] == 1.5f64) +
           (negative_doubles[1] == -1.5f64) +
           (double_mask[4] == -1) + (remainder[2] == 1) +
           (signed_quotient[5] == -2) +
           (signed_remainder[7] == -1) + (anded[0] == 2) +
           (ored[1] == 3) + (xored[2] == 1) +
           (negated[3] == -3) + (inverted[4] == -4) +
           (zero_mask[5] == -1) + (nonzero_mask[6] == 0) +
           (retyped[7] == 5u32) + (huge_loaded[2] == 13) +
           (huge_zero_mask[6] == -1) + (widened[2] == -4) +
           (narrowed[5] == -3) + (extended[1] == 4.0f64) +
           (truncated[4] == 4.0f32) +
           (transported[wide_lane] == 9) + (wide_cell[wide_lane] == 9) +
           (indexed[7] == 10) + (selected[3] == 5) +
           (called_sysv[0] == 6) + (called_ms[7] == 7) +
           (called_huge_sysv[2] == 12) + (called_huge_ms[6] == 13) +
           (changed[4] == 13) + (produced[5] == 15);
}
