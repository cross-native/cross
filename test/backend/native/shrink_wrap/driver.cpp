// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Checks every shrink-wrapping kernel against an independent C++ reference,
// calling it through a trampoline that verifies the preserved registers.
// The last check takes the path that calls the non-returning sw_fatal.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using U64 = std::uint64_t;
using I64 = std::int64_t;

#define SYSV __attribute__((sysv_abi))
#define MS __attribute__((ms_abi))

extern "C" {
SYSV U64 sw_canary_call(const void* function, U64 a, U64 b, U64 c);
SYSV double sw_canary_call_ms(const void* function, double x, U64 n);
SYSV U64 sw_fib(U64);
SYSV I64 sw_tak(I64, I64, I64);
SYSV U64 sw_tree(const U64*, U64, U64);
SYSV U64 sw_multi(U64, U64);
SYSV U64 sw_loop(U64, U64);
SYSV U64 sw_lanes(U64, U64);
SYSV U64 sw_one_path(U64);
SYSV U64 sw_noreturn(U64);
SYSV U64 sw_realigned(U64);
SYSV U64 sw_variadic(U64, ...);
MS double sw_float(double, U64);
MS U64 sw_local(U64, U64);

[[noreturn]] SYSV void sw_fatal(U64 value) {
    std::exit(value == (5000 ^ 0x5a5a) ? 0 : 3);
}
}

namespace {

U64 leaf(U64 a, U64 b) { return (a ^ (b << 7)) + (b >> 3) + 0x9e3779b97f4a7c15ULL; }

U64 fib(U64 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

I64 tak(I64 x, I64 y, I64 z) {
    return y >= x ? z : tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
}

U64 tree(const U64* values, U64 count, U64 node) {
    if (node >= count) return 0;
    const U64 value = values[node];
    const U64 left = tree(values, count, node * 2 + 1);
    const U64 right = tree(values, count, node * 2 + 2);
    return (left ^ (value * 3)) + right * 5 + value;
}

U64 multi(U64 a, U64 b) {
    if (a == 0) return b;
    if (b == 0) return a + 1;
    if (a > b) {
        const U64 first = leaf(a, b);
        if (first & 1) return first;
        return first + leaf(b, first) + a;
    }
    return a * b + 3;
}

U64 loop(U64 count, U64 seed) {
    if (count == 0) return seed;
    U64 sum = 0, mix = seed;
    for (U64 i = 0; i < count; ++i) {
        sum += leaf(mix, i);
        mix = mix * 3 + i;
    }
    return sum ^ mix;
}

U64 rotate(U64 value, unsigned bits) { return (value << bits) | (value >> (64 - bits)); }

U64 lanes(U64 rounds, U64 seed) {
    if (rounds == 0) return seed;
    U64 a = seed, b = seed ^ 1, c = seed + 2, d = seed * 3, e = seed ^ 4, f = seed + 5;
    U64 g = seed * 7, h = seed ^ 8, i = seed + 9, j = seed * 11, k = seed ^ 12, l = seed + 13;
    for (U64 r = 0; r < rounds; ++r) {
        a += b; d ^= a; d = rotate(d, 32);
        c += d; b ^= c; b = rotate(b, 24);
        e += f; h ^= e; h = rotate(h, 32);
        g += h; f ^= g; f = rotate(f, 24);
        i += j; l ^= i; l = rotate(l, 32);
        k += l; j ^= k; j = rotate(j, 24);
        a += f; l ^= a; c += h; j ^= c; e += l; b ^= e;
        g += j; d ^= g; i += b; h ^= i; k += d; f ^= k;
    }
    return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h ^ i ^ j ^ k ^ l;
}

U64 one_path(U64 x) { return (x & 1) ? leaf(x, 3) + x : x * 7; }

U64 noreturn_path(U64 x) { return x < 10 ? x + 1 : leaf(x, x) + x; }

U64 realigned(U64 x) {
    if (x < 3) return x;
    U64 cell = x;
    cell = cell * 5 + x;
    return (cell ^ x) + cell;
}

U64 variadic(U64 count, U64 first, U64 second) {
    return count == 0 ? 11 : leaf(first, count) + second;
}

double scale(double x, U64 n) { return x * 0.5 + static_cast<double>(n); }

U64 local(U64 n, U64 k) {
    if (n < 2) return n;
    U64 values[8];
    for (U64 i = 0; i < 8; ++i) values[i] = i * n + k;
    U64 sum = 0;
    for (U64 i = 0; i < 8; ++i) sum += values[(i * k) & 7] ^ i;
    return sum;
}

double floating(double x, U64 n) {
    if (n == 0) return x;
    const double first = scale(x, n);
    const double second = scale(first + x, n - 1);
    return first * second + x;
}

int failures = 0;

void check(const char* name, U64 input, U64 actual, U64 expected) {
    if (actual == expected) return;
    std::fprintf(stderr, "%s(%llu): got %llx, expected %llx\n", name,
                 static_cast<unsigned long long>(input),
                 static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    ++failures;
}

const void* address(auto function) { return reinterpret_cast<const void*>(function); }

} // namespace

int main() {
    static U64 values[1023];
    for (U64 index = 0; index < 1023; ++index) values[index] = index * 2654435761ULL;

    for (U64 n : {0ULL, 1ULL, 2ULL, 17ULL, 22ULL})
        check("fib", n, sw_canary_call(address(sw_fib), n, 0, 0), fib(n));
    const I64 tak_inputs[][3] = {{1, 5, 9}, {18, 12, 6}, {12, 8, 4}, {7, 7, 3}};
    for (const auto& input : tak_inputs) {
        check("tak", static_cast<U64>(input[0]),
              sw_canary_call(address(sw_tak), static_cast<U64>(input[0]),
                             static_cast<U64>(input[1]), static_cast<U64>(input[2])),
              static_cast<U64>(tak(input[0], input[1], input[2])));
    }
    for (U64 node : {0ULL, 5ULL, 1023ULL, 4000ULL})
        check("tree", node,
              sw_canary_call(address(sw_tree), reinterpret_cast<U64>(values), 1023, node),
              tree(values, 1023, node));
    const U64 multi_inputs[][2] = {{0, 9}, {5, 0}, {3, 8}, {9, 4}, {100, 7}, {1, 1}};
    for (const auto& input : multi_inputs)
        check("multi", input[0], sw_canary_call(address(sw_multi), input[0], input[1], 0),
              multi(input[0], input[1]));
    for (U64 count : {0ULL, 1ULL, 40ULL})
        check("loop", count, sw_canary_call(address(sw_loop), count, 5, 0), loop(count, 5));
    for (U64 rounds : {0ULL, 1ULL, 9ULL})
        check("lanes", rounds, sw_canary_call(address(sw_lanes), rounds, 12345, 0),
              lanes(rounds, 12345));
    for (U64 x : {6ULL, 7ULL, 0ULL, 99ULL})
        check("one_path", x, sw_canary_call(address(sw_one_path), x, 0, 0), one_path(x));
    for (U64 x : {3ULL, 10ULL, 500ULL, 1000ULL})
        check("noreturn", x, sw_canary_call(address(sw_noreturn), x, 0, 0), noreturn_path(x));
    for (U64 x : {1ULL, 3ULL, 77ULL})
        check("realigned", x, sw_canary_call(address(sw_realigned), x, 0, 0), realigned(x));
    check("variadic", 0, sw_canary_call(address(sw_variadic), 0, 100, 200), variadic(0, 100, 200));
    check("variadic", 2, sw_canary_call(address(sw_variadic), 2, 100, 200), variadic(2, 100, 200));
    for (U64 n : {0ULL, 3ULL, 8ULL}) {
        const double actual = sw_canary_call_ms(address(sw_float), 1.5, n);
        const double expected = floating(1.5, n);
        if (!(actual == expected)) {
            std::fprintf(stderr, "float(%llu): got %g, expected %g\n",
                         static_cast<unsigned long long>(n), actual, expected);
            ++failures;
        }
    }
    for (U64 n : {1ULL, 9ULL})
        check("local", n, sw_local(n, 5), local(n, 5));
    if (failures != 0) return 1;
    sw_canary_call(address(sw_noreturn), 5000, 0, 0);
    return 2;
}
