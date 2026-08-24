typedef i32 i32x4 [[ext_vector_type(4)]];
typedef f32 f32x4 [[ext_vector_type(4)]];
typedef u64 u64x2 [[ext_vector_type(2)]];
typedef f64 f64x2 [[ext_vector_type(2)]];
typedef i32 i32x8 [[ext_vector_type(8)]];
typedef f64 f64x8 [[ext_vector_type(8)]];

global i32 conversion_entry() {
    i8 signed8 = -7i8;
    i16 signed16 = -300i16;
    i32 signed32 = -2000000000;
    i64 signed64 = -5000000000000i64;
    f32 float8 = signed8;
    f32 float16 = signed16;
    f64 float32 = signed32;
    f64 float64 = signed64;

    u64 high = 9223372036854775808u64;
    f64 high_float = high;
    u64 high_back = high_float;
    u64 maximum_exact = 18446744073709549568u64;
    f64 maximum_float = maximum_exact;
    u64 maximum_back = maximum_float;

    i64 signed_back = -123.75f64;
    u32 unsigned_back = 4294967295.0f64;
    f64 mixed = -5 + 2.5f64;
    u64 mixed_high = 9223372036854775808.0f64 + 2048.0f64;
    f32 hexadecimal32 = 0x1.8p2f32;
    f64 hexadecimal64 = 0x1.fffffffffffffp63f64;
    f80 extended80 = high;
    u64 back80 = extended80;
    f128 extended128 = maximum_exact;
    u64 back128 = extended128;
    i64 truncated80 = -123.75f80;
    i64 truncated128 = -456.875f128;
    f80 mixed80 = -5 + 2.5f80;
    f128 mixed128 = -7 + 1.25f128;
    u128 power100 = 1267650600228229401496703205376u128;
    i128 negative_power100 = -1267650600228229401496703205376i128;
    f32 power100_f32 = power100;
    f64 power100_f64 = power100;
    f80 power100_f80 = power100;
    f128 power100_f128 = power100;
    f128 negative_power100_f128 = negative_power100;
    u128 power100_back = power100_f128;
    i128 negative_power100_back = negative_power100_f128;
    u128 power120_back = 0x1p120f128;
    i128 truncated128_wide = -123456789.75f128;
    u128 tie_input = 170141183460469231731687303715884122112u128;
    f128 tie_rounded = tie_input;
    u128 maximum128 = 340282366920938463463374607431768211455u128;
    f128 maximum128_rounded = maximum128;
    f80 increment80 = 1.5f80;
    f128 increment128 = 2.5f128;
    increment80++;
    ++increment128;

    i32x4 signed_vector = -3;
    f32x4 float_vector = signed_vector;
    i32x4 signed_vector_back = float_vector + 0.75f32;
    u64x2 high_vector = 9223372036854775808u64;
    f64x2 high_float_vector = high_vector;
    u64x2 high_vector_back = high_float_vector;

    i32x8 wide_vector = -9;
    f64x8 wide_float_vector = wide_vector;
    i32x8 wide_vector_back = wide_float_vector / 2.0f64;

    return (float8 == -7.0f32) + (float16 == -300.0f32) +
           (float32 == -2000000000.0f64) +
           (float64 == -5000000000000.0f64) +
           (high_float == 9223372036854775808.0f64) +
           (high_back == high) +
           (maximum_float == 18446744073709549568.0f64) +
           (maximum_back == maximum_exact) + (signed_back == -123i64) +
           (unsigned_back == 4294967295u32) + (mixed == -2.5f64) +
           (mixed_high == 9223372036854777856u64) +
           (hexadecimal32 == 6.0f32) +
           (hexadecimal64 == 18446744073709549568.0f64) +
           (extended80 == 9223372036854775808.0f80) +
           (back80 == high) +
           (extended128 == 18446744073709549568.0f128) +
           (back128 == maximum_exact) + (truncated80 == -123i64) +
           (truncated128 == -456i64) + (mixed80 == -2.5f80) +
           (mixed128 == -5.75f128) +
           (power100_f32 == 0x1p100f32) +
           (power100_f64 == 0x1p100f64) +
           (power100_f80 == 0x1p100f80) +
           (power100_f128 == 0x1p100f128) +
           (negative_power100_f128 == -0x1p100f128) +
           (power100_back == power100) +
           (negative_power100_back == negative_power100) +
           (power120_back == 1329227995784915872903807060280344576u128) +
           (truncated128_wide == -123456789i128) +
           (tie_rounded == 0x1p127f128) +
           (maximum128_rounded == 0x1p128f128) +
           (increment80 == 2.5f80) + (increment128 == 3.5f128) +
           (float_vector[2] == -3.0f32) +
           (signed_vector_back[1] == -2) +
           (high_float_vector[0] == 9223372036854775808.0f64) +
           (high_vector_back[1] == 9223372036854775808u64) +
           (wide_float_vector[6] == -9.0f64) +
           (wide_vector_back[7] == -4);
}
