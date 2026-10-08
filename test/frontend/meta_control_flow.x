// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaControlFlow {
[[eval_only]] static u32 controls(in u32 selector) {
    u32 total = 0u32;
    for (u32 index = 0u32; index < 4u32; ++index) {
        switch (index) {
        case 0u32: ++total; continue;
        case 1u32:
            switch (selector) { default: total += 3u32; }
            break;
        default: total += 5u32; break;
        }
        total += 7u32;
    }
    do {
        ++total;
        if (total < 37u32) continue;
        break;
    } while (total < 40u32);
    while (total < 39u32) { ++total; continue; }
    return total;
}
static $::meta::tokens helper(in $::meta::tokens input) {
    // The labels bind to their nearest switch despite intermediate control
    // statements. Untaken valid source must not run unsupported evaluator CFGs.
    if (0u32) {
        switch (0u32) {
            if (0u32) { case 1u32: break; }
            while (0u32) { default: continue; }
        }
        switch (0u32) { default: break; }
    }
    if (controls(5u32) != 39u32) return $::quote { 0u32 };
    return input;
}
static T unused_generic<T>(in T value) {
    break; // Source constraints wait for an actual instantiation.
    return value;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    return helper(input);
}
static u32 run() { return apply!(79u32); }
}
