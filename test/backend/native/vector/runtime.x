typedef i32 i32x4 [[ext_vector_type(4)]];
typedef u32 u32x4 [[ext_vector_type(4)]];
typedef i64 i64x2 [[ext_vector_type(2)]];
typedef u16 u16x8 [[vector_size(16)]];
typedef f32 f32x4 [[ext_vector_type(4)]];
typedef f64 f64x2 [[ext_vector_type(2)]];

global i32x4 vector_cell;
global volatile i32 vector_lane;

[[abi("sysv_abi"), noinline]]
static i32x4 vector_sysv(in i32x4 value) {
    return value + 1;
}

[[abi("ms_abi"), noinline]]
static i32 [[ext_vector_type(4)]] vector_ms(in i32 [[vector_size(16)]] value) {
    return (i32 [[ext_vector_type(4)]])(value + 2);
}

global i32 vector_entry() {
    register i32x4 fixed_lanes = 4;
    fixed_lanes[1] += 3;
    if (fixed_lanes[1]++ != 7 || --fixed_lanes[1] != 7 || fixed_lanes[0] != 4)
        return 0;
    [[ext_vector_type(4)]] i32 left = 3;
    i32x4 right = 2;
    i32x4 sum = left + right;
    i32x4 product = sum * right;
    i32x4 quotient = product / right;
    i32x4 remainder = product % 3;
    i32x4 negative = -7;
    i32x4 signed_quotient = negative / 3;
    i32x4 signed_remainder = negative % 3;
    i32x4 anded = left & right;
    i32x4 ored = left | right;
    i32x4 xored = left ^ right;
    i32x4 negated = -left;
    i32x4 inverted = ~left;
    i32x4 zero_mask = !(left - left);
    i32x4 nonzero_mask = !left;
    i32x4 mask = sum > right;
    i32x4 equal_mask = sum == 5;
    i32x4 unequal_mask = sum != right;
    i32x4 less_equal_mask = left <= sum;
    i32x4 greater_equal_mask = sum >= left;
    u32x4 high = 4294967295u32;
    u32x4 low = 1u32;
    i32x4 unsigned_mask = high > low;
    u32x4 retyped = sum;
    u16x8 bits = 3u16;
    u16x8 shifted = bits << 2;
    u16x8 unshifted = shifted >> 2;
    i64x2 wide_zero = 0;
    i64x2 wide_zero_mask = !wide_zero;
    f32x4 first = 1.5f32;
    f32x4 second = 0.5f32;
    f32x4 floats = (first + second) * 2.0f32;
    f64x2 doubles = 3.0f64 / 2.0f64;
    f64x2 negative_doubles = -doubles;
    i64x2 double_mask = doubles > 1.0f64;
    i32x4 called_sysv = vector_sysv(sum);
    i32x4 called_ms = vector_ms(sum);
    vector_cell = called_sysv;
    i32x4 *cell = &vector_cell;
    *cell = called_ms;
    i32x4 transported = cell[0];
    vector_lane = 2;
    transported[vector_lane] = 9;
    i32 scalar = quotient[0] + quotient[1] + quotient[2] + quotient[3];
    return scalar + (mask[0] == -1) + (shifted[7] == 12u16) +
           (floats[2] == 4.0f32) + (called_sysv[3] == 6) +
           (called_ms[1] == 7) + (transported[vector_lane] == 9) +
           (remainder[1] == 1) + (signed_quotient[2] == -2) +
           (signed_remainder[3] == -1) + (anded[0] == 2) +
           (ored[1] == 3) + (xored[2] == 1) + (negated[3] == -3) +
           (inverted[0] == -4) + (zero_mask[1] == -1) +
           (nonzero_mask[2] == 0) + (equal_mask[3] == -1) +
           (unequal_mask[0] == -1) + (less_equal_mask[1] == -1) +
           (greater_equal_mask[2] == -1) + (unsigned_mask[3] == -1) +
           (unshifted[6] == 3u16) + (retyped[0] == 5u32) +
           (wide_zero_mask[1] == -1) + (doubles[0] == 1.5f64) +
           (negative_doubles[1] == -1.5f64) + (double_mask[0] == -1);
}
