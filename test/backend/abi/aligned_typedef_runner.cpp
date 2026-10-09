// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstdio>

#if defined(__GNUC__) || defined(__clang__)
#define CROSS_SYSV __attribute__((sysv_abi))
#define CROSS_MS __attribute__((ms_abi))
#else
#define CROSS_SYSV
#define CROSS_MS
#endif

// C++ spellings of the Cross layouts: an aligned typedef pads its own size, so
// an over-aligned scalar that is not the last member is a padded wrapper.
struct one { alignas(16) std::uint32_t value; };
struct over { std::uint32_t a; alignas(16) std::uint32_t b; };
struct alignas(8) wide_u8 { std::uint8_t value; };
struct small { wide_u8 a; std::uint8_t b; };
struct floating { alignas(16) double x; };
struct alignas(16) wide_u32 { std::uint32_t value; };
struct array { wide_u32 x[2]; };
struct pair { std::uint32_t a, b; };

static_assert(sizeof(one) == 16 && sizeof(over) == 32 && sizeof(small) == 16);
static_assert(sizeof(floating) == 16 && sizeof(array) == 32);

extern "C" CROSS_SYSV one sysv_one(one, std::uint32_t);
extern "C" CROSS_SYSV over sysv_over(over);
extern "C" CROSS_SYSV small sysv_small(small);
extern "C" CROSS_SYSV floating sysv_floating(floating, double);
extern "C" CROSS_SYSV array sysv_array(array);
extern "C" CROSS_SYSV pair sysv_pair16(pair, std::uint32_t);
extern "C" CROSS_SYSV std::uint32_t sysv_scalars(std::uint32_t, std::uint32_t, std::uint32_t);
extern "C" CROSS_MS one ms_one(one, std::uint32_t);
extern "C" CROSS_MS small ms_small(small);
extern "C" CROSS_MS pair ms_pair16(pair, std::uint32_t);
extern "C" CROSS_MS std::uint32_t ms_scalars(std::uint32_t, std::uint32_t, std::uint32_t);
extern "C" int cross_aligned_calls();

int main() {
    unsigned failures = 0;
    const auto check = [&](bool condition, const char* name) {
        if (!condition) {
            std::fprintf(stderr, "aligned typedef ABI mismatch: %s\n", name);
            ++failures;
        }
    };
    one o{};
    o.value = 40;
    check(sysv_one(o, 2).value == 42, "SysV padded INTEGER eightbyte");
    over v{};
    v.a = 1;
    v.b = 2;
    const auto rv = sysv_over(v);
    check(rv.a == 2 && rv.b == 4, "SysV memory");
    small s{};
    s.a.value = 5;
    s.b = 6;
    const auto rs = sysv_small(s);
    check(rs.a.value == 6 && rs.b == 8, "SysV two INTEGER eightbytes");
    floating f{};
    f.x = 1.5;
    check(sysv_floating(f, 2.0).x == 3.5, "SysV padded SSE eightbyte");
    array a{};
    a.x[0].value = 10;
    a.x[1].value = 20;
    const auto ra = sysv_array(a);
    check(ra.x[0].value == 11 && ra.x[1].value == 22, "SysV padded array elements");
    const auto rp = sysv_pair16({3, 4}, 5);
    check(rp.a == 8 && rp.b == 9, "SysV aligned record typedef as its base type");
    check(sysv_scalars(1, 2, 3) == 123, "SysV aligned scalars as their base type");
    check(ms_one(o, 3).value == 43, "Win64 sixteen-byte record by reference");
    const auto ms = ms_small(s);
    check(ms.a.value == 6 && ms.b == 8, "Win64 padded record by reference");
    const auto mp = ms_pair16({3, 4}, 6);
    check(mp.a == 9 && mp.b == 10, "Win64 aligned record typedef as its base type");
    check(ms_scalars(4, 5, 6) == 456, "Win64 aligned scalars as their base type");
    check(cross_aligned_calls() == 1, "Cross ABI calls");
    return failures != 0;
}
