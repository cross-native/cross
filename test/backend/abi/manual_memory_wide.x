// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

typedef i32 i32x8 [[ext_vector_type(8)]];
typedef i64 i64x8 [[ext_vector_type(8)]];

global void f80_stack(inout f80 value "stack") {
    value += 0.5f80;
}

global f80 f80_stack_result(in f80 value "stack") -> "stack+32" {
    return value * 2.0f80;
}

global void f80_indirect(inout f80 value "*r10") {
    value -= 1.0f80;
}

global void f80_auto_observe(in f80 value,
                             out f80 observed "stack") {
    observed = value + 2.0f80;
}

global void vector_stack(inout i32x8 value "stack") {
    value += 2;
}

global void vector_indirect(inout i32x8 value "*r11") {
    value *= 3;
}

global i32x8 vector_stack_result(in i32x8 value "stack") -> "stack+32" {
    return value + 1;
}

global void vector_stack_observe(in i32x8 value "stack",
                                 out i32 observed "eax") {
    observed = value[3];
}

global void vector512_stack(inout i64x8 value "stack") {
    value += 4;
}

global i32 manual_memory_wide_entry() {
    f80 a = 1.5f80;
    f80_stack(a);
    f80 b = f80_stack_result(3.0f80);
    f80 c = 7.0f80;
    f80_indirect(c);
    f80 observed = 0.0f80;
    f80_auto_observe(4.0f80, observed);

    i32x8 value = 4;
    vector_stack(value);
    vector_indirect(value);
    i32x8 result = vector_stack_result(value);
    i32 auto_observed = 0;
    i32x8 auto_value = 11;
    vector_stack_observe(auto_value, auto_observed);

    i64x8 wide = 5;
    vector512_stack(wide);

    return (a == 2.0f80) + (b == 6.0f80) + (c == 6.0f80) +
           (observed == 6.0f80) + (value[0] == 18) +
           (result[7] == 19) + (auto_observed == 11) +
           (wide[0] == 9) + (wide[7] == 9);
}
