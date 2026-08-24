global u64 reduction_values[13];
global f64 polynomial_values[7];

[[noinline]]
static u64 optimized_reduction(in const u64 *values, in uptr count) {
    u64 result = 0;
    uptr index = 0;
    while (index < count) {
        result = result + values[index];
        index = index + 1;
    }
    return result;
}

[[noinline]]
static f64 optimized_polynomial(in const f64 *values, in uptr count) {
    f64 result = 0.0f64;
    uptr index = 0;
    while (index < count) {
        f64 value = values[index];
        f64 term = ((value * 0.5f64 + 1.25f64) * value - 0.75f64) *
                   value + 0.125f64;
        result = result + term;
        index = index + 1;
    }
    return result;
}

[[noinline, link_name("nested_reduction")]]
static u64 nested_reduction(in const u64 *values, in uptr count,
                            in uptr rounds) {
    u64 result = 0;
    uptr round = 0;
    u64 round_value = 0;
    while (round < rounds) {
        u64 sum = 0;
        uptr index = 0;
        while (index < count) {
            sum = sum + values[index];
            index = index + 1;
        }
        result = result + (sum ^ round_value);
        round = round + 1;
        round_value = round_value + 1;
    }
    return result;
}

[[noinline, link_name("affine_xor_reduction")]]
static u64 affine_xor_reduction(in const u64 *values, in uptr count) {
    u64 result = 0;
    uptr index = 0;
    while (index < count) {
        result = result ^ (values[index] ^ (index * 3));
        index = index + 1;
    }
    return result;
}

[[noinline, link_name("byte_swap_reduction")]]
static u64 byte_swap_reduction(in const u64 *values, in uptr count) {
    u64 result = 0;
    uptr index = 0;
    while (index < count) {
        u64 value = values[index];
        u64 swapped = ((value & 0x00ff00ff00ff00ffu64) << 8) |
                      ((value & 0xff00ff00ff00ff00u64) >> 8);
        result = result ^ swapped;
        index = index + 1;
    }
    return result;
}

[[noinline, link_name("constant_product_reduction")]]
static u64 constant_product_reduction(in const u64 *values, in uptr count) {
    u64 result = 0;
    uptr index = 0;
    while (index < count) {
        result = result +
                 values[index] * 0x9e3779b185ebca87u64;
        index = index + 1;
    }
    return result;
}

global i32 vector_loop_entry() {
    uptr index = 0;
    while (index < 13) {
        reduction_values[index] = index + 1;
        index = index + 1;
    }
    index = 0;
    while (index < 7) {
        polynomial_values[index] = 1.0f64;
        index = index + 1;
    }

    return (optimized_reduction(reduction_values, 0) == 0) +
           (optimized_reduction(reduction_values, 1) == 1) +
           (optimized_reduction(reduction_values, 3) == 6) +
           (optimized_reduction(reduction_values, 4) == 10) +
           (optimized_reduction(reduction_values, 5) == 15) +
           (optimized_reduction(reduction_values, 13) == 91) +
           (optimized_polynomial(polynomial_values, 0) == 0.0f64) +
           (optimized_polynomial(polynomial_values, 1) == 1.125f64) +
           (optimized_polynomial(polynomial_values, 3) == 3.375f64) +
           (optimized_polynomial(polynomial_values, 4) == 4.5f64) +
           (optimized_polynomial(polynomial_values, 5) == 5.625f64) +
           (optimized_polynomial(polynomial_values, 7) == 7.875f64) +
           (nested_reduction(reduction_values, 5, 3) == 42) +
           (affine_xor_reduction(reduction_values, 13) == 17) +
           (byte_swap_reduction(reduction_values, 13) == 256) +
           (constant_product_reduction(reduction_values, 13) ==
            4447377314062335485u64);
}
