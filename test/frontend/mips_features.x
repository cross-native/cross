// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if $::has_feature($::feature::integer128)
#error MIPS must not advertise unavailable integer128 lowering
#endif
#if $::has_feature($::feature::binary128_storage)
#error MIPS must not advertise unavailable binary128 storage
#endif
#if $::has_feature($::feature::binary128_arithmetic)
#error MIPS must not advertise unavailable binary128 arithmetic
#endif
#if $::has_feature($::feature::fixed_vectors)
#error MIPS must not advertise unavailable fixed vectors
#endif
#if $::has_feature($::feature::variadics)
#error MIPS must not advertise unavailable variadic state lowering
#endif
#if $::has_feature($::feature::atomics)
#error baseline MIPS must not advertise atomics without LL/SC
#endif
#if $::has_feature($::feature::thread_local)
#error MIPS must not advertise unavailable thread-local lowering
#endif

global i32 mips_features_consistent = 1;
