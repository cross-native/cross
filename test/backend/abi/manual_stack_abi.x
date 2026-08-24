// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void stack_inout(inout i32 value "stack") {
    value += 3;
}

global void stack_fixed(in i32 source "stack+8", out i32 result "stack+16") {
    result = source * 2;
}

global void lifo_cells(inout i32 first "push=>pop",
                       in i32 second "push=>discard",
                       out i32 third "reserve=>pop") {
    first += second;
    third = first + 4;
}

global i32 stack_result(in i32 value "stack") {
    return value + 1;
}

global i32 stack_stored_result(in i32 value "eax") -> "stack+32" {
    return value + 5;
}

global void mixed_stack_register(in i32 amount "eax", inout i32 value "stack",
                                 out i32 observed "*r11") {
    value += amount;
    observed = value + 1;
}

global void stack_to_register(inout i32 value "stack+24=>r10d") {
    value += 2;
}

global void register_to_stack(inout i32 value "r11d=>stack+24") {
    value *= 3;
}

[[stack_cleanup("callee")]]
global void callee_cleanup(in i32 value "push=>discard", out i32 result "r10d") {
    result = value + 2;
}

global i32 manual_stack_entry() {
    i32 simple = 4;
    stack_inout(simple);

    i32 fixed = 0;
    stack_fixed(5, fixed);

    i32 first = 2;
    i32 third = 0;
    lifo_cells(first, 3, third);

    i32 ordinary = stack_result(6);
    i32 stored_ordinary = stack_stored_result(4);

    i32 mixed = 3;
    i32 observed = 0;
    mixed_stack_register(2, mixed, observed);

    i32 to_register = 1;
    stack_to_register(to_register);

    i32 to_stack = 2;
    register_to_stack(to_stack);

    i32 cleaned = 0;
    callee_cleanup(5, cleaned);

    return simple + fixed + first + third + ordinary + stored_ordinary + mixed + observed +
           to_register + to_stack + cleaned;
}
