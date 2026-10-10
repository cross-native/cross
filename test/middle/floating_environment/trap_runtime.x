// Runs with the FCSR flush (FS) and invalid-operation enable (EV) bits set,
// the environment that the trapping-fp profile declares. The startup prints
// T on an exception and P when test_entry returns zero.

union bits32 {
    f32 value;
    u32 bits;
};

union bits64 {
    f64 value;
    u64 bits;
};

u32 bits_of(in f32 value) {
    union bits32 cell;
    cell.value = value;
    return cell.bits;
}

f32 from_bits(in u32 bits) {
    union bits32 cell;
    cell.bits = bits;
    return cell.value;
}

u64 bits_of64(in f64 value) {
    union bits64 cell;
    cell.value = value;
    return cell.bits;
}

f64 from_bits64(in u64 bits) {
    union bits64 cell;
    cell.bits = bits;
    return cell.value;
}

[[noinline]]
global f32 guarded_div(in f32 a, in f32 b) {
    return b != 0.0f32 ? a / b : 0.0f32;
}

[[noinline]]
global f32 guarded_loop(in const f32 *v, in u32 n, in f32 d) {
    f32 sum = 0.0f32;
    for (u32 i = 0u32; i < n; ++i) {
        sum += v[i] / d;
    }
    return sum;
}

[[noinline]]
global u32 count_zero(in u32 n, in f32 x) {
    u32 count = 0u32;
    for (u32 i = 0u32; i < n; ++i) {
        if (!x) count += i;
    }
    return count;
}

[[noinline]]
global f32 product(in f32 a, in f32 b) {
    return a * b;
}

[[noinline]]
global f32 quotient(in f32 a, in f32 b) {
    return a / b;
}

[[noinline]]
global u32 is_zero(in f32 x) {
    return !x;
}

// Negation flips the sign bit and raises nothing, also on a signaling NaN.
[[noinline]]
global f32 negated(in f32 x) {
    return -x;
}

[[noinline]]
global f64 negated64(in f64 x) {
    return -x;
}

[[noinline]]
global f32 minimum(in f32 x, in f32 y) {
    return $::fmin(x, y);
}

global f32 tiny = 1.0e-30f32 * 1.0e-10f32;

// A signaling NaN: in the legacy MIPS encoding, the most significant fraction
// bit is set. The quiet NaN is the default one.
global u32 signaling_nan = 0x7fc00000u32;
global u32 quiet_nan = 0x7fbfffffu32;
global u64 signaling_nan64 = 0x7ff8000000012345u64;

global u32 test_entry() {
    u32 flush_and_invalid = 0x01000800u32;
    $::_ctc1(flush_and_invalid, 31);
#if defined(UNGUARDED)
    // Traps: the invalid operation is enabled.
    return bits_of($::runtime(quotient(0.0f32, 0.0f32))) == 0u32 ? 0u32 : 1u32;
#elif defined(SIGNALING)
    // Traps: comparing a signaling NaN is invalid.
    return $::runtime(is_zero(from_bits(signaling_nan)));
#elif defined(FMIN_SIGNALING)
    // Traps: a signaling right operand is invalid even when the left one is
    // a quiet NaN.
    return bits_of($::runtime(minimum(from_bits(quiet_nan), from_bits(signaling_nan)))) == 0u32
        ? 0u32 : 1u32;
#else
    f32 values[1] = {1.0f32};
    if (bits_of($::runtime(guarded_div(1.0f32, 0.0f32))) != 0u32) return 1;
    if (bits_of($::runtime(guarded_div(0.0f32, 0.0f32))) != 0u32) return 2;
    if (bits_of($::runtime(guarded_loop(values, 0u32, 0.0f32))) != 0u32) return 3;
    if ($::runtime(count_zero(0u32, from_bits(signaling_nan))) != 0u32) return 4;
    // The FPU flushes the product as translation-time evaluation flushed tiny.
    if (bits_of(tiny) != 0u32) return 5;
    if (bits_of($::runtime(product(1.0e-30f32, 1.0e-10f32))) != 0u32) return 6;
    if (bits_of($::runtime(negated(from_bits(signaling_nan)))) != 0xffc00000u32) return 7;
    if (bits_of($::runtime(negated(from_bits(1u32)))) != 0x80000001u32) return 8;
    if (bits_of64($::runtime(negated64(from_bits64(signaling_nan64)))) !=
        0xfff8000000012345u64) return 9;
    if (bits_of64($::runtime(negated64(-2.5))) != 0x4004000000000000u64) return 10;
    if (bits_of($::runtime(minimum(from_bits(quiet_nan), 1.5f32))) != 0x3fc00000u32) return 11;
    return 0;
#endif
}
