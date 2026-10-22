// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Initializer tables on bare metal: init_tables_start.s walks the ELF
// arrays as a C runtime would. Constructors and destructors record a digit
// each; the entry points check the order, lower priorities constructing
// first and destroying last.

typedef void (*entry)();

global u32 constructed;
global u32 destroyed;

[[constructor]] static void plain() { constructed = constructed * 10u32 + 4u32; }
[[constructor(101)]] static void early() { constructed = constructed * 10u32 + 1u32; }
[[constructor(300)]] global void late() { constructed = constructed * 10u32 + 3u32; }
static void by_object() { constructed = constructed * 10u32 + 2u32; }
[[constructor(200)]] static entry object_entry = by_object;

[[destructor]] static void finish() { destroyed = destroyed * 10u32 + 1u32; }
[[destructor(101)]] static void last() { destroyed = destroyed * 10u32 + 3u32; }
[[destructor(200)]] static void second() { destroyed = destroyed * 10u32 + 2u32; }

global u32 init_tables_entry() {
    return constructed == 1234u32 ? 0u32 : 0x10000u32 + constructed;
}

global u32 init_tables_finished() {
    return destroyed == 123u32 ? 0u32 : 0x20000u32 + destroyed;
}
