// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[link_name("external_function")]]
i64 external_function_decl(in i64 value);

[[link_name("external_data")]]
i64 external_data_decl;

static i64 local_data = 3;

[[runtime_only, link_name("relocation_models_entry")]]
global i64 relocation_models_entry() {
    return external_function_decl(external_data_decl + local_data);
}

[[runtime_only, link_name("relocation_address_entry")]]
global i64 *relocation_address_entry() {
    return &external_data_decl;
}
