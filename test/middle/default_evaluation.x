// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

static u64 add(in u64 left, in u64 right) {
    return left + right;
}

static i32 short_circuit(in i32 divisor) {
    return divisor == 0 || 12 / divisor > 1;
}

static i32 compound_once() {
    i32 value = 1;
    value += ++value;
    return value;
}

[[eval_only]]
static u64 evaluated_add(in u64 left, in u64 right) {
    return add(left, right);
}

[[runtime_only]]
static u64 runtime_add(in u64 left, in u64 right) {
    return left + right;
}

[[runtime_only]]
static u64 runtime_seed() {
    return 10u64;
}

global u64 required_global = add(20u64, 22u64);
global i32 required_short_circuit = $::eval(short_circuit(0));
global i32 required_compound_once = $::eval(compound_once());

global u64 staged_calls_entry() {
    u64 automatic = add(20u64, 22u64);
    u64 required = $::eval(add(1u64, 2u64));
    u64 deferred = $::runtime(add(3u64, 4u64));
    u64 forbidden = runtime_add(5u64, 6u64);
    u64 only = evaluated_add(7u64, 8u64);
    u64 unknown = add(runtime_seed(), 1u64);
    return automatic + required + deferred + forbidden + only + unknown;
}

global i32 evaluation_entry() {
    return required_global == 42u64 &&
           required_short_circuit == 1 &&
           required_compound_once == 3 &&
           staged_calls_entry() == 89u64;
}
