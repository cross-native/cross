// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
namespace EnumChain {
static uptr next(in uptr value) { return value + 1uptr; }
enum Number0 [[underlying(uptr)]] { value0 = sizeof(uptr) };
// Reuse the shared row indices; these dependencies run in the opposite order
// from the physical-layout fixture, since enum values follow declaration order.
#define CROSS_LAYOUT_ROW(HEAD, TAIL, KIND) enum Number##TAIL [[underlying(uptr)]] { value##TAIL = next((uptr)value##HEAD) };
#include "syntax_layout_rows.inc"
#undef CROSS_LAYOUT_ROW
[[syntax_expander]] static $::meta::tokens retain(in $::meta::syntax_match input) {
    if ((uptr)value240 != sizeof(uptr) + 240uptr) return $::quote { wrong_enum_value };
    return $::quote { $::unquote($::syntax::node(input, "definition")) };
}
syntax Retain : item { prefix "retained"; match definition:function_def; expand retain; }
syntax Retain;
retained [[noinline]] static u32 run(in u32 seed) {
    return seed + (u32)value240 - (u32)(sizeof(uptr) + 240uptr);
}
}
static volatile u32 seed = 61u32;
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() { return EnumChain::run(seed); }
