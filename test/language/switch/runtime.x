// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
[[noinline]] global i32 case_value(in i32 value) { return value + 11; }
[[noinline]] global i32 case_other(in i32 value) { return value * 3; }

global i32 dispatch(in i32 op) {
    i32 result = 0;
    switch (op) {
    case 0:
        result += case_value(op);
    case 1:
        result += 2;
        break;
    case 2:
        result = 7;
        switch (op) {
        case 2:
            result += 5;
            break;
        default:
            result += 99;
        }
        break;
    case 3:
        result = case_other(op);
        break;
    default:
        result = 40;
    }
    return result;
}

global i32 dense(in i32 op) {
    i32 result = 0;
    switch (op) {
    case 0: result += case_value(op); break;
    case 1: result += case_other(op); break;
    case 2: result += case_value(op); break;
    case 3: result += case_other(op); break;
    case 4: result += case_value(op); break;
    case 5: result += case_other(op); break;
    case 6: result += case_value(op); break;
    case 7: result += case_other(op); break;
    case 8: result += case_value(op); break;
    case 9: result += case_other(op); break;
    default: result = -1;
    }
    return result;
}

global i32 dense_unreachable(in i32 op) {
    switch (op) {
    case 0: return case_value(op);
    case 1: return case_other(op);
    case 2: return case_value(op);
    case 3: return case_other(op);
    case 4: return case_value(op);
    case 5: return case_other(op);
    case 6: return case_value(op);
    case 7: return case_other(op);
    case 8: return case_value(op);
    case 9: return case_other(op);
    default: $::unreachable();
    }
}

global i32 dense_assumed(in i32 op) {
    $::assume(op < 10u32);
    switch (op) {
    case 0: return case_value(op);
    case 1: return case_other(op);
    case 2: return case_value(op);
    case 3: return case_other(op);
    case 4: return case_value(op);
    case 5: return case_other(op);
    case 6: return case_value(op);
    case 7: return case_other(op);
    case 8: return case_value(op);
    case 9: return case_other(op);
    default: return -2;
    }
}

enum Mode [[underlying(u32)]] { Zero = 0, One = 1, Two = 2 };
global i32 enum_dispatch(in enum Mode mode) {
    switch (mode) {
    // Enumerators are intentionally matched by their underlying values.
    case 0u32: return 10;
    case 1u32: return 20;
    default: return 30;
    }
}

global i32 switch_entry(in i32 op) {
    i32 total = dispatch(op) + dense(op);
    if (op >= 0 && op < 10) total += dense_assumed(op);
    total += enum_dispatch(1u32);
    i32 i = 0;
    while (i < 3) {
        switch (op) {
        case 11:
            ++i;
            continue;
        default:
            break;
        }
        total += i;
        ++i;
    }
    return total;
}

global i32 no_default(in i32 op) {
    i32 result = 0;
    switch (op) {
    case 4:
        result = 44;
        break;
    }
    return result;
}

global i32 single_case(in i32 op) {
    switch (op) case 1: return 71;
    return 0;
}

global i32 selector_counter = 0;
global i32 next_selector() {
    ++selector_counter;
    return selector_counter;
}
global i32 selector_once() {
    switch (next_selector()) {
    case 1: return 10;
    default: return selector_counter;
    }
}

[[eval_only]] static i32 eval_switch(in i32 op) {
    i32 result = 0;
    switch (op) {
    case -1:
    case 0:
        result = 10;
        result += 3;
        break;
    case 1: {
        i32 result = 5;
        switch (result) {
        case 5: result += 7; break;
        default: return 99;
        }
        return result;
    }
    default: result = 20;
    }
    switch (op) { result = 99; }
    return result;
}
global i32 evaluated_switch() {
    return eval_switch(-1) + eval_switch(0) + eval_switch(1) + eval_switch(8);
}

global i32 conditional_assume(in u32 op, in u32 flag) {
    if (flag) $::assume(op < 10u32);
    switch (op) {
    case 0: return case_value(op);
    case 1: return case_other(op);
    case 2: return case_value(op);
    case 3: return case_other(op);
    case 4: return case_value(op);
    case 5: return case_other(op);
    case 6: return case_value(op);
    case 7: return case_other(op);
    case 8: return case_value(op);
    case 9: return case_other(op);
    default: return -3;
    }
}
