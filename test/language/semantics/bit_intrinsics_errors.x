// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(FLOATING)
global u32 bad(in f64 x) { return $::clz(x); }
#elif defined(BOOLEAN)
global u32 bad(in bool x) { return $::popcount(x); }
#elif defined(POINTER)
global u32 bad(in u8 *x) { return $::ctz(x); }
#elif defined(COUNT)
global u32 bad(in u32 x) { return $::rotr(x, 1.0); }
#elif defined(ROTATE_ARITY)
global u32 bad(in u32 x) { return $::rotl(x); }
#elif defined(SWAP_ARITY)
global u32 bad(in u32 x) { return $::bswap(x, x); }
#elif defined(EVALUATED)
$::static_assert($::ctz(2.0f32) == 1u32, "floating operand");
#elif defined(UNTAKEN)
global u32 bad(in u32 x) { return x != 0u32 ? x : $::popcount(1.5); }
#else
global u32 good(in u32 x) { return $::clz(x); }
#endif
