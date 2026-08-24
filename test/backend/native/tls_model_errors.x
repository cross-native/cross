// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[thread_local, tls_model("global-dynamic")]]
static i64 resolver_tls;

[[tls_model("local-exec")]]
static i64 missing_thread_local;
