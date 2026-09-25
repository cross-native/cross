// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static $::meta::bytes rotate(in $::meta::bytes source) {
    uptr count = $::meta::len(source);
    return $::meta::concat($::meta::slice(source, 1u32, count - 1u32),
                          $::meta::slice(source, 0u32, 1u32));
}

static uptr invert_bytes(in const u8 *source, in uptr length,
                         in u8 *destination, in uptr capacity) {
    uptr cursor = 0u32;
    const u8 *one_past = source + length;
    while (cursor < length && cursor < capacity) {
        destination[cursor] = *(source + cursor) ^ 255u32;
        ++cursor;
    }
    return cursor;
}

static u8 read_one(in const u8 *source) { return source[0u32]; }
static i8 read_signed(in const i8 *source) { return source[0u32]; }

static $::meta::bytes signed_output() {
    $::meta::buffer output = $::meta::alloc(1u32);
    i8 *pointer = (i8 *)$::meta::data(output);
    pointer[0u32] = -1i8;
    return $::meta::freeze(output, 1u32);
}

static $::meta::bytes inverted(in $::meta::bytes source) {
    $::meta::buffer output = $::meta::alloc($::meta::len(source));
    uptr length = invert_bytes($::meta::data(source), $::meta::len(source),
                               $::meta::data(output), $::meta::cap(output));
    return $::meta::freeze(output, length);
}

static uptr buffer_capacity(in $::meta::buffer value) {
    return $::meta::cap(value);
}

static $::meta::buffer make_empty_buffer() {
    return $::meta::alloc(0u32);
}

[[eval_only]] static uptr empty_buffer_capacity() {
    $::meta::buffer value = make_empty_buffer();
    $::meta::buffer alias = value;
    uptr capacity = buffer_capacity(alias);
    $::meta::bytes empty = $::meta::freeze(value, 0u32);
    return capacity + $::meta::len(empty);
}

[[macro]] static $::meta::tokens copy_asset(in $::meta::tokens input) {
    return $::quote { $::unquote(input) };
}

global const u8 original[] = $::embed("payload.bin");
global const u8 copied[] = copy_asset! { $::embed("payload.bin") };
global u8 rotated[5] = rotate($::embed("payload.bin"));
global u8 encoded[] = inverted($::embed("payload.bin"));
global u8 encoded_slice[3] = inverted($::meta::slice($::embed("payload.bin"), 1u32, 3u32));
global const u8 signed_encoded[] = signed_output();
global uptr asset_size = $::meta::len($::embed("payload.bin"));
global uptr static_size = sizeof(original);
global uptr empty_capacity = empty_buffer_capacity();
global u8 first_byte = $::meta::at($::embed("payload.bin"), 0u32);
global u8 zero_byte = $::meta::at($::embed("payload.bin"), 1u32);
global u8 high_byte = $::meta::at($::embed("payload.bin"), 3u32);
global u8 second_byte = read_one($::meta::data($::embed("payload.bin")) + 1u32);
global i8 signed_high = read_signed((const i8 *)$::meta::data(
    $::meta::slice($::embed("payload.bin"), 3u32, 1u32)));
global i32 signed_extended = read_signed((const i8 *)$::meta::data(
    $::meta::slice($::embed("payload.bin"), 3u32, 1u32)));
