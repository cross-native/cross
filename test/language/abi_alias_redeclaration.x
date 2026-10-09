// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Declarations agree when their ABI names select the same registered entry.
[[abi("sysv_abi")]] global u64 alias_linux(in u64 a, in u64 b);
[[abi("linux"), noinline]] global u64 alias_linux(in u64 a, in u64 b) { return a * 10u64 + b; }
[[abi("ms")]] static u64 alias_windows(in u64 a, in u64 b);
[[abi("windows"), noinline]] static u64 alias_windows(in u64 a, in u64 b) { return a * 100u64 + b; }
[[abi("cross_abi")]] u64 alias_cross(in u64 a, in u64 b);
[[abi("cross"), noinline]] u64 alias_cross(in u64 a, in u64 b) { return a * 1000u64 + b; }
typedef u64 (*sysv_callback)(in u64 a, in u64 b) [[abi("sysv_abi")]];
global sysv_callback alias_pointer = alias_linux;

global u32 abi_alias_entry() {
    return alias_linux(1u64, 2u64) == 12u64 && alias_windows(3u64, 4u64) == 304u64 &&
           alias_cross(5u64, 6u64) == 5006u64 && alias_pointer(7u64, 8u64) == 78u64 ? 1u32 : 0u32;
}
