// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline, link_name("allegrex_entry")]]
global i32 allegrex_entry(in u32 word, in u32 count, in i32 condition);

[[runtime_only, noinline, link_name("mips_pair_entry")]]
global i32 mips_pair_entry();

// Keep the import stub observable to Cross. PPSSPP replaces its first two
// instructions before execution, so this store is never reached at runtime.
global volatile u32 psp_stub_observable;

// PPSSPP's PSP loader replaces the first two instructions of this function
// using the import metadata emitted by module_info.s.
[[runtime_only, noinline, section(".text.psp.stub"),
  link_name("sceKernelExitGame")]]
global void psp_exit_stub() {
    psp_stub_observable = 1u32;
}

[[runtime_only, noinline, section(".text.boot"), link_name("_start")]]
global i32 psp_test_start() {
    i32 result = allegrex_entry(0x80000001u32, 1u32, 1i32);
    i32 pairs = mips_pair_entry();
    if ((result != 6i32) || (pairs != 0xfffffi32)) {
        while (1) {}
    }
    psp_exit_stub();
    return 0i32;
}
