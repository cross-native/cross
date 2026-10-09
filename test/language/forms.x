// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Member offsets, incomplete arrays, for-clause lists, exhaustive records,
// fixed addresses, attribute regions, and function-pointer casts.

struct header {
    u8 tag;
    u16 length;
};

struct packet {
    struct header head;
    u32 words[3];
    u64 stamp;
};

$::static_assert($::offsetof(struct packet, words[1]) ==
                 $::offsetof(struct packet, words) + sizeof(u32), "element step");
static u8 stamp_prefix[$::offsetof(struct packet, stamp)];

static uptr third_word() { return $::offsetof(struct packet, words[2]); }

static u32 check_offsets() {
    struct packet value;
    const u8 *base = (const u8 *)(const void *)&value;
    return (uptr)((const u8 *)(const void *)&value.words[2] - base) == third_word() &&
           (uptr)((const u8 *)(const void *)&value.head.length - base) ==
               $::offsetof(struct packet, head.length) &&
           sizeof(stamp_prefix) == $::offsetof(struct packet, stamp);
}

// Completed by the definition below; `imported` is defined by a separately
// compiled object.
u32 squares[];
global u32 squares[] = {0, 1, 4, 9, 16};
u32 imported[];

static u32 check_arrays() {
    const u32 *first = squares;
    const u32 *other = imported;
    return sizeof(squares) == 5 * sizeof(u32) && first[3] == 9 &&
           imported[2] == 30 && other[0] == 10;
}

static u32 check_for_lists() {
    u32 i;
    u32 j;
    u32 total = 0;
    for (i = 0, j = 10; i < j; ++i, j -= 2) total += j - i;
    u32 sum = 0;
    for (u32 a = 1, b = 2; a < 20; a += b, b += 1) {
        if (a == 6) continue;
        sum += a;
    }
    return total == 22 && i == 4 && j == 2 && sum == 29;
}

struct pair [[exhaustive]] {
    u32 first;
    u32 second;
};

struct holder {
    struct pair inner;
    u32 loose;
};

static struct pair static_pair = { .second = 2, .first = 1 };

static u32 check_exhaustive() {
    struct pair local = { 3, 4 };
    struct holder outer = { .inner = { 5, 6 } };
    return static_pair.first + static_pair.second + local.first + local.second +
           outer.inner.first + outer.inner.second + outer.loose == 21;
}

#if $::has_abi("sysv_abi")
#define REGION_ABI "sysv_abi"
#elif $::target::pointer_bytes == 4
#define REGION_ABI "cross32"
#else
#define REGION_ABI "n64"
#endif

[[abi(REGION_ABI)]] {
    typedef u32 (*region_binary)(in u32 a, in u32 b);
    global u32 region_subtract(in u32 a, in u32 b) { return a - b; }
    [[noinline, section(".text.forms")]] {
        global u32 region_scaled(in u32 a, in u32 b) { return a * 10 + b; }
    }
}

typedef u32 (*explicit_binary)(in u32 a, in u32 b) [[abi(REGION_ABI)]];

static u32 check_regions() {
    region_binary subtract = region_subtract;
    explicit_binary same = subtract;
    explicit_binary scaled = region_scaled;
    return same(10, 3) == 7 && scaled(4, 2) == 42;
}

typedef void (*erased)();
typedef u32 (*unary)(in u32 x);

static u32 bump(in u32 x) { return x + 7; }

static erased erased_table[2] = { (erased)bump, (erased)&bump };
static uptr bump_address = (uptr)bump;
static unary relocated = (unary)(uptr)bump;

static u32 check_function_casts() {
    const unary restored = (unary)erased_table[0];
    const unary from_address = (unary)bump_address;
    const uptr round_trip = (uptr)restored;
    const unary again = (unary)round_trip;
    return restored(1) == 8 && from_address(2) == 9 && again(3) == 10 && relocated(4) == 11 &&
           round_trip == bump_address && (uptr)erased_table[1] == bump_address;
}

// A fixed-address object and a function supplied at a fixed address by the
// MIPS startup code; host runs only compile them.
u32 fixed_cell [[address(0x80600000)]];
u32 fixed_add(in u32 a, in u32 b) [[address(0x80100100)]];

global u32 check_fixed() {
    fixed_cell = 40;
    fixed_cell += 2;
    return fixed_cell == 42 && fixed_add(fixed_cell, 8) == 50;
}

#if $::has_abi("sysv_abi")
// The x86-64 raw lowering of for-clause lists.
[[naked, clobber("flags")]]
static u64 raw_pairs() -> "rax" {
    register u64 result "rax" = 0, left "rcx", right "rdx";
    for (left = 0, right = 10; left < right; left = left + 1, right = right - 1) {
        result = result + 1;
    }
    $::_ret();
}
#endif

global u32 forms_entry() {
    u32 result = check_offsets() && check_arrays() && check_for_lists() &&
                 check_exhaustive() && check_regions() && check_function_casts();
#if $::has_abi("sysv_abi")
    result = result && raw_pairs() == 5;
#else
    result = result && check_fixed();
#endif
    return result;
}
