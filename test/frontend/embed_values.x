// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static $::meta::bytes rotate(in $::meta::bytes source) {
    uptr count = $::meta::len(source);
    return $::meta::concat($::meta::slice(source, 1u32, count - 1u32),
                          $::meta::slice(source, 0u32, 1u32));
}

[[macro]] static $::meta::tokens copy_asset(in $::meta::tokens input) {
    return $::quote { $::unquote(input) };
}

global const u8 original[] = $::embed("payload.bin");
global const u8 copied[] = copy_asset! { $::embed("payload.bin") };
global u8 rotated[5] = rotate($::embed("payload.bin"));
global uptr asset_size = $::meta::len($::embed("payload.bin"));
global uptr static_size = sizeof(original);
global u8 first_byte = $::meta::at($::embed("payload.bin"), 0u32);
global u8 zero_byte = $::meta::at($::embed("payload.bin"), 1u32);
global u8 high_byte = $::meta::at($::embed("payload.bin"), 3u32);
