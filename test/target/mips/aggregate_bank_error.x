// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
struct triple { u64 a; u64 b; u64 c; };
// Cross32 selects flattening here, but has only four integer result channels.
// Exhaustion is a model error; it must not silently reselect the memory rule.
[[abi("cross32")]] global struct triple too_many_results(in struct triple input) {
    return input;
}
