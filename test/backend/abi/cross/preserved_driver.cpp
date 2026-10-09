// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Checks the Cross-ABI functions of preserved_callee.x and preserved_caller.x
// against an independent C++ reference, calling each through a trampoline
// that verifies every register the Cross ABI preserves.

#include <cstdint>
#include <cstdio>
#include <initializer_list>

using U64 = std::uint64_t;

extern "C" {
__attribute__((sysv_abi)) U64 cross_canary_call(const void* function, U64 a,
                                                U64 b, U64 c);
// Cross-ABI functions; this file only takes their addresses.
void cross_lanes();
void cross_fib();
void cross_keep();
void cross_variadic();
void cross_hard();
void cross_drive();
void cross_drive_calls();
}

namespace {

U64 leaf(U64 a, U64 b) { return (a ^ (b << 7)) + (b >> 3) + 0x9e3779b97f4a7c15ULL; }

U64 rotate(U64 value, unsigned bits) { return (value << bits) | (value >> (64 - bits)); }

U64 lanes(U64 rounds, U64 seed) {
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

U64 fib(U64 n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

U64 keep(U64 count, U64 seed) {
    U64 a = seed, b = seed ^ 1, c = seed + 2, d = seed * 3, e = seed ^ 4, f = seed + 5;
    for (U64 i = 0; i < count; ++i) {
        a += leaf(a, i);
        b ^= leaf(b, a);
        c += leaf(c, b);
        d ^= leaf(d, c);
        e += leaf(e, d);
        f ^= leaf(f, e);
    }
    return a ^ b ^ c ^ d ^ e ^ f;
}

U64 variadic(std::initializer_list<U64> arguments) {
    const U64 count = arguments.size();
    U64 sum = count;
    U64 mix = count * 0x100000001b3ULL;
    for (const U64 argument : arguments) {
        sum += leaf(argument, mix);
        mix = mix * 3 + sum;
    }
    return sum ^ mix;
}

U64 hard(U64 seed) {
    U64 kept = seed * 3;
    U64 other = seed ^ 5;
    kept += leaf(kept, other);
    other ^= leaf(other, kept);
    return kept + other;
}

U64 manual(U64 value, U64 other, U64 plain) { return (value * 5) ^ (other + plain); }

U64 clobber(U64 value) { return value * 2 + 1; }

U64 drive(U64 count, U64 seed) {
    U64 a = seed, b = seed * 3, c = seed ^ 0x55, d = seed + 7, e = seed << 9, f = seed - 11;
    for (U64 i = 0; i < count; ++i) {
        a += leaf(a, i);
        b ^= clobber(b);
        c += leaf(c, a);
        d ^= clobber(d + c);
        e += keep(1, e);
        f ^= clobber(f ^ e);
    }
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f;
}

U64 drive_calls(U64 seed) {
    const U64 kept0 = seed * 5 + 3;
    const U64 kept1 = seed ^ 0x0123456789abcdefULL;
    const U64 kept2 = seed << 9;
    const U64 sum = variadic({seed, kept0, kept1, kept2});
    const U64 first = manual(kept0, kept1, sum);
    const U64 second = manual(first, kept2, kept0);
    return sum + first * 3 + (kept1 ^ second) + kept2 + clobber(kept0);
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

const void* address(void (*function)()) { return reinterpret_cast<const void*>(function); }

} // namespace

int main() {
    for (U64 rounds : {0ULL, 1ULL, 9ULL})
        check("lanes", rounds, cross_canary_call(address(cross_lanes), rounds, 12345, 0),
              lanes(rounds, 12345));
    for (U64 n : {0ULL, 1ULL, 17ULL})
        check("fib", n, cross_canary_call(address(cross_fib), n, 0, 0), fib(n));
    for (U64 count : {0ULL, 3ULL})
        check("keep", count, cross_canary_call(address(cross_keep), count, 77, 0),
              keep(count, 77));
    check("variadic", 2, cross_canary_call(address(cross_variadic), 2, 100, 200),
          variadic({100, 200}));
    check("hard", 9, cross_canary_call(address(cross_hard), 9, 0, 0), hard(9));
    for (U64 count : {0ULL, 1ULL, 6ULL})
        check("drive", count, cross_canary_call(address(cross_drive), count, 5, 0),
              drive(count, 5));
    check("drive_calls", 21, cross_canary_call(address(cross_drive_calls), 21, 0, 0),
          drive_calls(21));
    return failures == 0 ? 0 : 1;
}
