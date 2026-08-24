// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global i32 has_aes = $::has_instruction($::_aesenc);
global i32 has_f16c = $::has_instruction($::_vcvtps2ph256);
global i32 has_avx512dq = $::has_instruction($::_vpmullq512);
global i32 has_avx512dq_vl = $::has_instruction($::_vpmullq256);
