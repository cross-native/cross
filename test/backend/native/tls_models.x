// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[thread_local, tls_model("local-exec"), link_name("local_tls")]]
static i64 local_tls = 5;

[[thread_local, tls_model("initial-exec"), link_name("external_tls")]]
i64 external_tls;

[[runtime_only, link_name("tls_models_entry")]]
global i64 tls_models_entry() {
    local_tls = local_tls + 1;
    return local_tls + external_tls;
}

[[runtime_only, link_name("tls_address_entry")]]
global i64 *tls_address_entry() {
    return &local_tls;
}
