// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

profile "pointer-profile" {
    abi = "odd_abi";
    mangling = "simple";
    optimization = "O2";
}

mangling "erase-generic-arguments" {
    entity = name;
    label = name;
    generic = entity;
}
