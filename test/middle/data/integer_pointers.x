// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A static pointer initialized from an integer holds that address, with no
// relocation, on its own and inside aggregates. At run time a narrower
// integer extends by its signedness and a wider one keeps its address bits.

typedef void (*handler)(void);
struct device { volatile u32 *registers; handler isr; uptr size; };

global u32 *plain = (u32 *)0x1000;
global volatile u32 *mmio = (volatile u32 *)0x40001000;
global handler vector = (handler)0x2000uptr;
global struct device devices[2] = {
    {(volatile u32 *)0x1000, (handler)0x2040uptr, 16uptr},
    {(volatile u32 *)(0x1000 + 0x100), (handler)0uptr, 32uptr},
};
global const u8 *offsets[3] = {(const u8 *)0x10 + 1, (const u8 *)0, (const u8 *)-1};

global volatile u32 address32 = 0x80001000u32;
global volatile i32 negative32 = -16i32;
global volatile u16 address16 = 0x9000u16;
global volatile i8 negative8 = -2i8;
global volatile u64 address64 = 0x1_0000_0005u64;
[[noinline]] global uptr from_u32(in u32 x) { return (uptr)(u8 *)x; }
[[noinline]] global uptr from_i32(in i32 x) { return (uptr)(const u32 *)x; }
[[noinline]] global uptr from_u16(in u16 x) { return (uptr)(void *)x; }
[[noinline]] global uptr from_i8(in i8 x) { return (uptr)(volatile u16 *)x; }
[[noinline]] global uptr from_u64(in u64 x) { return (uptr)(u8 *)x; }
global uptr evaluated_i8(in i8 x) { return (uptr)(const u32 *)x; }
$::static_assert(evaluated_i8(-2i8) == ~1uptr, "a narrower integer extends by its signedness");

global u32 test_entry() {
    if ((uptr)plain != 0x1000uptr) return 1u32;
    if ((uptr)mmio != 0x40001000uptr) return 2u32;
    if ((uptr)vector != 0x2000uptr) return 3u32;
    if ((uptr)devices[0].registers != 0x1000uptr ||
        (uptr)devices[0].isr != 0x2040uptr || devices[0].size != 16uptr) return 4u32;
    if ((uptr)devices[1].registers != 0x1100uptr ||
        (uptr)devices[1].isr != 0uptr || devices[1].size != 32uptr) return 5u32;
    if ((uptr)offsets[0] != 0x11uptr || (uptr)offsets[1] != 0uptr ||
        (uptr)offsets[2] != ~0uptr) return 6u32;
    if (from_u32(address32) != 0x80001000uptr) return 7u32;
    if (from_i32(negative32) != (uptr)-16iptr) return 8u32;
    if (from_u16(address16) != 0x9000uptr) return 9u32;
    if (from_i8(negative8) != ~1uptr) return 10u32;
    if (from_u64(address64) != (uptr)address64) return 11u32;
    return 0u32;
}
