// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Constructors record a letter each, which main prints, so the line shows
// that they ran before main and in which order; destructors print a line
// each when the C runtime calls them at exit.

[[abi("sysv_abi")]] {
    [[link_name("write")]] iptr c_write(in i32 fd, in const void *buffer, in uptr count);
}
[[abi("sysv_abi")]] typedef void (*entry)();

static u8 trace[16];
static u32 traced;

static void note(in u8 code) {
    trace[traced] = code;
    traced += 1u32;
}

[[constructor]] static void plain() { note('c'); }
[[constructor(101)]] static void early() { note('1'); }
[[constructor(200)]] global void middle() { note('2'); }
[[abi("sysv_abi"), constructor(150)]] static void same_abi() { note('s'); }
[[constructor(65535)]] static void explicit_default() { note('e'); }

// A region gives its functions the attribute, but not the u32 object.
[[constructor(250)]] {
    static void from_region() { note('r'); }
    static u32 region_value = 1u32;
}

[[abi("sysv_abi")]] static void by_object() { note('o'); }
[[constructor(120)]] static entry object_entry = by_object;
[[abi("sysv_abi")]] static void first_of_two() { note('a'); }
[[abi("sysv_abi")]] static void second_of_two() { note('b'); }
[[constructor(130)]] static const entry pair[2] = {first_of_two, second_of_two};

[[destructor]] static void finish() { c_write(1, "d\n", 2uptr); }
[[destructor(101)]] static void last() { c_write(1, "d101\n", 5uptr); }
[[destructor(200)]] static void second() { c_write(1, "d200\n", 5uptr); }

[[abi("sysv_abi")]]
global i32 main() {
    trace[traced] = '\n';
    c_write(1, trace, (uptr)traced + 1uptr);
    return 0;
}
