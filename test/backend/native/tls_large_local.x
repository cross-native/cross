// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[thread_local, tls_model("local-exec"), link_name("large_local_tls")]]
static i64 large_local_tls = 9;

[[runtime_only, link_name("large_local_tls_address")]]
global i64 *large_local_tls_address() {
    return &large_local_tls;
}
