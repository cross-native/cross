// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only]]
static u64 runtime_seed(in u64 value) {
    return value + 1;
}

static u64 enters_runtime_only(in u64 value) {
    return runtime_seed(value);
}

[[eval_only]]
static u64 compile_seed(in u64 value) {
    return value + 2;
}

global u64 forced_failure = $::eval(enters_runtime_only(10));
global u64 forbidden_barrier = $::runtime(runtime_seed(20));

global u64 runtime_calls_eval_only() {
    return $::runtime(compile_seed(30));
}

global u64 contradictory_staging() {
    return $::runtime($::eval(enters_runtime_only(40)));
}
