// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#if !$::has_feature($::feature::atomics)
#error MIPS LL/SC configurations must advertise atomics
#endif

global i32 mips_llsc_features_consistent = 1;
