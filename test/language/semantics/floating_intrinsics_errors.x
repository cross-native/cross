// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if defined(INTEGER)
global f64 bad(in u32 x) { return $::sqrt(x); }
#elif defined(MIXED)
global f64 bad(in f64 x, in f32 y) { return $::fmin(x, y); }
#elif defined(ARITY)
global f64 bad(in f64 x) { return $::copysign(x); }
#elif defined(EVALUATED)
$::static_assert($::fmax(1.0f32, 2.0) == 2.0, "mixed operand types");
#elif defined(BINARY128)
global f128 bad(in f128 x, in f128 y) { return $::fmax(x, y); }
#else
global f64 bad(in f64 x) { return $::sqrt(x); }
#endif
