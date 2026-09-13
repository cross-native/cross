// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if !$::has_feature($::feature::thread_local)
#error x86-64 must advertise its implemented thread-local lowering
#endif
#if !$::has_feature($::feature::integer128)
#error x86-64 must advertise integer128 lowering
#endif
#if !$::has_feature($::feature::fixed_vectors)
#error x86-64 must advertise fixed vectors
#endif
#if !$::has_feature($::feature::atomics)
#error x86-64 must advertise atomics
#endif
#if !$::has_feature($::feature::variadics)
#error x86-64 must advertise variadics
#endif

global i32 x86_features_consistent = 1;
