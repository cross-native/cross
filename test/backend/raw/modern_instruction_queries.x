// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

i32 has_adx = $::has_instruction($::_adcx);
i32 has_vector_crypto = $::has_instruction($::_vpclmulqdq256);
i32 has_vbmi2 = $::has_instruction($::_vpshldvd512);
