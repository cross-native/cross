// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[raw_inline]]
static u64 needs_local(in u64 value) {
    u64 copy = value;
    return copy + 1u64;
}

[[naked, clobber("flags")]]
global void missing_scratch(in u64 value "r9", out u64 result "r8") {
    result = needs_local(value);
    $::_ret();
}

[[raw_inline]]
static u64 uses_stack(in u64 value) {
    stack u64 copy = value;
    return copy;
}

[[naked, clobber("r10")]]
global void stack_object(in u64 value "r9", out u64 result "r8") {
    result = uses_stack(value);
    $::_ret();
}

static u64 runtime_call(in u64 value) {
    return value;
}

[[raw_inline]]
static u64 calls_runtime(in u64 value) {
    return runtime_call(value);
}

[[naked]]
global void surviving_call(in u64 value "r9", out u64 result "r8") {
    result = calls_runtime(value);
    $::_ret();
}

[[raw_inline]]
static u64 recursive(in u64 value) {
    if (value == 0) return 0;
    return recursive(value - 1);
}

[[naked, clobber("r10", "flags")]]
global void recursive_call(in u64 value "r9", out u64 result "r8") {
    result = recursive(value);
    $::_ret();
}

[[raw_inline]]
static f64 unsupported_float_conversion(in f32 value) {
    return value;
}

[[naked, clobber("xmm6")]]
global void mixed_float(in f32 value "xmm4", out f64 result "xmm5") {
    result = unsupported_float_conversion(value);
    $::_ret();
}
