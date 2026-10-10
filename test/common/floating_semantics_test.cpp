// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/floating_semantics.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <initializer_list>

using cross::UInt128;
using namespace cross::floating;

namespace {

constexpr auto invalid_flag = exception_set(Exception::Invalid);
constexpr auto divide_flag = exception_set(Exception::DivideByZero);
constexpr auto overflow_flag = exception_set(Exception::Overflow);
constexpr auto underflow_flag = exception_set(Exception::Underflow);
constexpr auto inexact_flag = exception_set(Exception::Inexact);
constexpr auto denormal_flag = exception_set(Exception::DenormalOperand);

// The flags the host FPU raised since the last feclearexcept.
ExceptionSet host_flags() {
    ExceptionSet result{};
    if (std::fetestexcept(FE_INVALID)) result |= invalid_flag;
    if (std::fetestexcept(FE_DIVBYZERO)) result |= divide_flag;
    if (std::fetestexcept(FE_OVERFLOW)) result |= overflow_flag;
    if (std::fetestexcept(FE_UNDERFLOW)) result |= underflow_flag;
    if (std::fetestexcept(FE_INEXACT)) result |= inexact_flag;
    return result;
}

// The host detects tininess after rounding and flags underflow_flag only when the
// result is also inexact_flag, so it reports a subset of the tiny results.
bool same_flags(ExceptionSet actual, ExceptionSet host) {
    const auto compared = invalid_flag | divide_flag | overflow_flag | inexact_flag;
    return (actual & compared) == (host & compared) &&
           ((host & underflow_flag) == 0 || (actual & underflow_flag) != 0);
}

Value f32_bits(std::uint32_t bits) { return {UInt128{bits}, Format::Binary32}; }

} // namespace

int main() {
    const auto f32 = Format::Binary32;
    const auto f64 = Format::Binary64;
    const auto f80 = Format::Extended80;
    const auto f128 = Format::Binary128;
    const auto one = *parse("1.0", f32);
    const auto half_ulp = *parse("0x1p-24", f32);
    const auto above_half = *parse("0x1.8p-24", f32);
    if (binary(Operation::Add, one, half_ulp, f32).bits != UInt128{0x3f800000}) return 1;
    if (binary(Operation::Add, one, above_half, f32).bits != UInt128{0x3f800001}) return 2;
    if (parse("1.000000059604644775390625", f32)->bits !=
        UInt128{0x3f800000}) return 2;
    if (parse("1.000000059604644775390626", f32)->bits !=
        UInt128{0x3f800001}) return 2;
    if (parse("0x1p-149", f32)->bits != UInt128{1}) return 3;
    if (parse("0x1p-150", f32)->bits != UInt128{}) return 4;
    if (parse("0x1.8p-150", f32)->bits != UInt128{1}) return 5;
    if (binary(Operation::Divide, one, *parse("3.0", f32), f32).bits !=
        UInt128{0x3eaaaaab}) return 6;
    if (parse("1.5", f80)->bits != UInt128{0xc000000000000000ULL, 0x3fff}) return 7;
    if (parse("1.5", f128)->bits != UInt128{0, 0x3fff800000000000ULL}) return 8;
    if (convert(*parse("1.5", f128), f64).bits != UInt128{0x3ff8000000000000ULL}) return 9;
    if (from_integer(UInt128{0xffffffff}, 32, true, f32).bits !=
        UInt128{0xbf800000}) return 10;
    if (to_integer(*parse("-1.5", f64), 32, true) != UInt128{0xffffffff}) return 11;
    if (to_integer(*parse("-0.5", f64), 32, false) != UInt128{}) return 12;
    const auto negative_zero = *parse("-0.0", f32);
    if (negative_zero.bits != UInt128{0x80000000} || nonzero(negative_zero)) return 13;
    if (!compare(Comparison::Equal, negative_zero, *parse("0.0", f32))) return 14;
    const auto infinity = binary(Operation::Divide, one, *parse("0.0", f32), f32);
    if (infinity.bits != UInt128{0x7f800000}) return 15;
    if (binary(Operation::Multiply, one, infinity, f32).bits !=
        UInt128{0x7f800000}) return 15;
    const auto invalid = binary(Operation::Subtract, infinity, infinity, f32);
    if (compare(Comparison::Equal, invalid, invalid) ||
        !compare(Comparison::NotEqual, invalid, invalid)) return 16;
    if (binary(Operation::Add, *parse("1.0", f128),
               *parse("0x1p-112", f128), f128).bits !=
        UInt128{1, 0x3fff000000000000ULL}) return 17;

    // Exceptions and the environment.
    const auto raised_by = [&](Operation op, Value a, Value b) {
        ExceptionSet raised{};
        (void)binary(op, a, b, f32, &raised);
        return raised;
    };
    const auto zero = *parse("0.0", f32);
    const auto max = f32_bits(0x7f7fffff);
    const auto tiny_product = binary(Operation::Multiply, *parse("1.0e-30", f32),
                                     *parse("1.0e-10", f32), f32);
    const auto denormal_value = *parse("0x1p-140", f32);
    if (raised_by(Operation::Divide, zero, zero) != invalid_flag) return 30;
    if (raised_by(Operation::Divide, one, zero) != divide_flag) return 31;
    if (raised_by(Operation::Divide, infinity, zero) != 0 ||
        binary(Operation::Divide, infinity, zero, f32).bits != infinity.bits ||
        binary(Operation::Divide, zero, infinity, f32).bits != zero.bits) return 31;
    if (raised_by(Operation::Subtract, infinity, infinity) != invalid_flag) return 32;
    if (raised_by(Operation::Multiply, max, *parse("2.0", f32)) != (overflow_flag | inexact_flag))
        return 33;
    if (tiny_product.bits != UInt128{71362} || !denormal(tiny_product) ||
        raised_by(Operation::Multiply, *parse("1.0e-30", f32), *parse("1.0e-10", f32)) !=
            (underflow_flag | inexact_flag)) return 34;
    // An exact tiny result is still tiny.
    if (raised_by(Operation::Multiply, *parse("0x1p-70", f32), *parse("0x1p-70", f32)) !=
        underflow_flag) return 35;
    if (raised_by(Operation::Divide, one, *parse("3.0", f32)) != inexact_flag) return 36;
    if (raised_by(Operation::Add, one, one) != 0) return 37;
    if (raised_by(Operation::Add, denormal_value, one) != (denormal_flag | inexact_flag))
        return 38;
    if (denormal(zero) || denormal(one) || !denormal(f32_bits(0x80000001))) return 39;
    const auto quiet_nan = f32_bits(0x7fc00000);
    const auto signaling_nan = f32_bits(0x7fa00000);
    if (raised_by(Operation::Add, quiet_nan, one) != 0 ||
        raised_by(Operation::Add, signaling_nan, one) != invalid_flag) return 40;
    ExceptionSet raised{};
    if (compare(Comparison::Less, quiet_nan, one, &raised) || raised != invalid_flag) return 41;
    raised = 0;
    if (compare(Comparison::Equal, quiet_nan, one, &raised) || raised != 0) return 42;
    if (compare(Comparison::NotEqual, signaling_nan, one, &raised) == false ||
        raised != invalid_flag) return 43;
    raised = 0;
    (void)compare(Comparison::Equal, denormal_value, one, &raised);
    if (raised != denormal_flag) return 44;
    raised = 0;
    (void)convert(*parse("1.0e-40", f64), f32, &raised);
    if (raised != (underflow_flag | inexact_flag)) return 45;
    raised = 0;
    (void)convert(*parse("1.0e300", f64), f32, &raised);
    if (raised != (overflow_flag | inexact_flag)) return 46;
    raised = 0;
    (void)convert(signaling_nan, f64, &raised);
    if (raised != invalid_flag) return 47;
    raised = 0;
    (void)from_integer(UInt128{16777217}, 32, true, f32, &raised);
    if (raised != inexact_flag) return 48;
    raised = 0;
    (void)from_integer(UInt128{16777216}, 32, true, f32, &raised);
    if (raised != 0) return 48;
    raised = 0;
    if (to_integer(*parse("1.5", f32), 32, true, &raised) != UInt128{1} ||
        raised != inexact_flag) return 49;
    raised = 0;
    if (to_integer(*parse("-2.0", f32), 32, true, &raised) != UInt128{0xfffffffe} ||
        raised != 0) return 49;
    raised = 0;
    if (to_integer(denormal_value, 32, true, &raised) != UInt128{} ||
        raised != (denormal_flag | inexact_flag)) return 50;

    const Environment masked{};
    const Environment n64{invalid_flag | denormal_flag, true};
    auto result = tiny_product;
    if (trap(masked, underflow_flag | inexact_flag, &result) || result.bits != tiny_product.bits)
        return 51;
    if (trap(n64, underflow_flag | inexact_flag, &result) || result.bits != UInt128{}) return 52;
    result = negate(tiny_product);
    if (trap(n64, underflow_flag | inexact_flag, &result) || result.bits != UInt128{0x80000000})
        return 53;
    result = tiny_product;
    if (trap(Environment{inexact_flag, true}, 0, &result) != Exception::Inexact) return 54;
    if (trap(n64, invalid_flag) != Exception::Invalid ||
        trap(n64, denormal_flag | invalid_flag) != Exception::DenormalOperand ||
        trap(n64, divide_flag | overflow_flag | inexact_flag)) return 55;
    if (exception_name(Exception::DivideByZero) != "divide-by-zero" ||
        exception_name(Exception::DenormalOperand) != "denormal-operand") return 56;
    if (!permits_new_intermediates(Environment{divide_flag}) ||
        permits_new_intermediates(n64) || !permits_contraction(n64) ||
        permits_contraction(Environment{underflow_flag})) return 57;

    // Independent host IEEE operations cross-check thousands of exact target
    // results and their exceptions; exceptional NaN payloads are
    // intentionally not specified.
    std::uint64_t state = 0x6a09e667f3bcc909ULL;
    const auto next = [&] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int i = 0; i < 4000; ++i) {
        const auto bits_a = static_cast<std::uint32_t>(next());
        const auto bits_b = static_cast<std::uint32_t>(next());
        volatile float a = std::bit_cast<float>(bits_a);
        volatile float b = std::bit_cast<float>(bits_b);
        const Value soft_a{UInt128{bits_a}, f32};
        const Value soft_b{UInt128{bits_b}, f32};
        for (const auto op : {Operation::Add, Operation::Subtract,
                              Operation::Multiply, Operation::Divide}) {
            std::feclearexcept(FE_ALL_EXCEPT);
            volatile float host = op == Operation::Add ? a + b
                : op == Operation::Subtract ? a - b
                : op == Operation::Multiply ? a * b : a / b;
            const auto host_raised = host_flags();
            ExceptionSet soft_raised{};
            const auto actual = binary(op, soft_a, soft_b, f32, &soft_raised).bits.low;
            const auto expected = std::bit_cast<std::uint32_t>(static_cast<float>(host));
            const bool both_nan = (actual & 0x7f800000U) == 0x7f800000U &&
                (actual & 0x007fffffU) != 0 &&
                (expected & 0x7f800000U) == 0x7f800000U &&
                (expected & 0x007fffffU) != 0;
            if (actual != expected && !both_nan) return 18;
            if (!same_flags(soft_raised, host_raised)) return 20;
        }
    }
    for (int i = 0; i < 2000; ++i) {
        const auto bits_a = next();
        const auto bits_b = next();
        volatile double a = std::bit_cast<double>(bits_a);
        volatile double b = std::bit_cast<double>(bits_b);
        const Value soft_a{UInt128{bits_a}, f64};
        const Value soft_b{UInt128{bits_b}, f64};
        for (const auto op : {Operation::Add, Operation::Subtract,
                              Operation::Multiply, Operation::Divide}) {
            std::feclearexcept(FE_ALL_EXCEPT);
            volatile double host = op == Operation::Add ? a + b
                : op == Operation::Subtract ? a - b
                : op == Operation::Multiply ? a * b : a / b;
            const auto host_raised = host_flags();
            ExceptionSet soft_raised{};
            const auto actual = binary(op, soft_a, soft_b, f64, &soft_raised).bits.low;
            const auto expected = std::bit_cast<std::uint64_t>(static_cast<double>(host));
            const bool both_nan = (actual & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL &&
                (actual & 0x000fffffffffffffULL) != 0 &&
                (expected & 0x7ff0000000000000ULL) == 0x7ff0000000000000ULL &&
                (expected & 0x000fffffffffffffULL) != 0;
            if (actual != expected && !both_nan) return 19;
            if (!same_flags(soft_raised, host_raised)) return 21;
        }
        // Narrowing and integer conversions.
        std::feclearexcept(FE_ALL_EXCEPT);
        volatile float narrowed = static_cast<float>(a);
        auto host_raised = host_flags();
        ExceptionSet soft_raised{};
        const auto soft_narrowed = convert(soft_a, f32, &soft_raised);
        if (std::bit_cast<std::uint32_t>(static_cast<float>(narrowed)) !=
                soft_narrowed.bits.low &&
            !std::isnan(static_cast<float>(narrowed))) return 22;
        if (!same_flags(soft_raised, host_raised)) return 23;
        const auto integer = static_cast<std::int64_t>(bits_b);
        std::feclearexcept(FE_ALL_EXCEPT);
        volatile std::int64_t integer_operand = integer;
        volatile float from_host = static_cast<float>(integer_operand);
        host_raised = host_flags();
        soft_raised = 0;
        const auto soft_from = from_integer(UInt128{bits_b}, 64, true, f32, &soft_raised);
        if (std::bit_cast<std::uint32_t>(static_cast<float>(from_host)) != soft_from.bits.low ||
            !same_flags(soft_raised, host_raised)) return 24;
        const volatile float in_range = std::bit_cast<float>(
            static_cast<std::uint32_t>((bits_a & 0x807fffffU) | (static_cast<std::uint32_t>(
                100 + bits_a % 56) << 23)));
        std::feclearexcept(FE_ALL_EXCEPT);
        volatile std::int32_t truncated = static_cast<std::int32_t>(in_range);
        host_raised = host_flags();
        soft_raised = 0;
        const auto soft_truncated = to_integer(
            f32_bits(std::bit_cast<std::uint32_t>(static_cast<float>(in_range))), 32, true,
            &soft_raised);
        if (!soft_truncated ||
            soft_truncated->low != static_cast<std::uint32_t>(truncated) ||
            !same_flags(soft_raised, host_raised)) return 25;
    }
    return 0;
}
