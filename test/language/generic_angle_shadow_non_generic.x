// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static T item<T>(in T value) { return value; }
namespace imported {
    i32 item = 2i32;
}
namespace use_import {
    using imported;
    global i32 bad() { return item::<i32>(1i32); }
}
