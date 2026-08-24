// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[naked]]
global void raw_duplicate_label() {
again:
again:
    $::_ret();
}
