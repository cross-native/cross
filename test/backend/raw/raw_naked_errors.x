// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

void external_raw_call();

[[naked]]
global void raw_ordinary_return() {
    return;
}

[[naked]]
global void raw_ordinary_call() {
    external_raw_call();
    $::_ret();
}

[[naked]]
global void raw_automatic_object() {
    u64 value;
    $::_ret();
}

[[naked]]
global void raw_unbalanced_stack() {
    register u64 value "rax";
    $::_push(value);
    $::_ret();
}

[[naked]]
global void raw_stack_underflow() {
    register u64 value "rax";
    $::_pop(value);
    $::_ret();
}

[[naked]]
global void raw_after_terminator() {
    $::_ret();
    $::_ret();
}

[[naked]]
global void raw_wrong_arity() {
    $::_ret(1);
}

[[naked]]
global void raw_fallthrough() {
}
