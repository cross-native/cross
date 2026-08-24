// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only]]
global i32 composition_echo(in i32 value) {
    return value;
}

global i32 composition_call() {
    return composition_echo(37);
}
