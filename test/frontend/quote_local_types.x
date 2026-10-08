// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A quoted name bound to a block-scope typedef or enum tag of the executing
// helper denotes that type where the generated code is placed.
[[macro]]
static $::meta::tokens local_sizes(in $::meta::tokens input) {
    typedef u16 Choice;
    enum Mode { Off = 3, On = 9 };
    typedef u32 Lanes[4];
    typedef u64 *Pointer;
    return $::quote {
        (sizeof(Choice) + sizeof(enum Mode) * 10uptr + sizeof(Lanes) * 100uptr +
         sizeof(Pointer) * 10000uptr)
    };
}

typedef u64 Choice;

// A value declared in an inner block shadows the helper's typedef.
[[macro]]
static $::meta::tokens shadowed(in $::meta::tokens input) {
    typedef u16 Wide;
    {
        u32 Wide = 1u32;
        return $::quote { Wide };
    }
}

global u32 Wide = 77u32;

global u32 quote_local_types_entry() {
    return local_sizes!() == 2uptr + 40uptr + 1600uptr + 80000uptr &&
           sizeof(Choice) == 8uptr && shadowed!() == 77u32;
}
