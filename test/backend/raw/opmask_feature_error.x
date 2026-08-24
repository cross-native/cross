// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked]]
global void opmask_feature_error() {
    register u64 mask "k1";
    $::_knotw(mask, mask);
    $::_ret();
}
