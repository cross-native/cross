// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked, link_name("raw_avx_instruction")]]
global void raw_avx_instruction() {
    $::_vzeroupper();
    $::_ret();
}
