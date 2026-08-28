// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

[[runtime_only, noinline, link_name("ifconv_compare")]]
global i32 ifconv_compare(in i32 left, in i32 right,
                          in i32 truth, in i32 falsity) {
    return left < right ? truth + 3 : falsity - 5;
}

[[runtime_only, noinline, link_name("ifconv_return")]]
global i32 ifconv_return(in i32 left, in i32 right,
                         in i32 truth, in i32 falsity) {
    if (left > right) {
        return truth ^ 7;
    } else {
        return falsity + 9;
    }
}

[[runtime_only, noinline, link_name("ifconv_small")]]
global u8 ifconv_small(in bool choose, in u8 truth, in u8 falsity) {
    return choose ? truth : falsity;
}

[[runtime_only, noinline, link_name("ifconv_expensive")]]
global u64 ifconv_expensive(in bool choose, in u64 left, in u64 right) {
    if (choose) {
        return left * 3 + right * 5 + 1;
    } else {
        return left * 7 + right * 9 + 2;
    }
}

[[runtime_only, noinline, link_name("ifconv_balance")]]
global u64 ifconv_balance(in bool choose, in u64 value) {
    if (choose) {
        return value * 3;
    } else {
        return value + 1;
    }
}

[[runtime_only, noinline, link_name("ifconv_masked_load")]]
global u64 ifconv_masked_load(in const u64 *source, in u64 result) {
    u64 value = source[0];
    if ((value & 1) != 0) {
        return result + value * 3 + 1;
    } else {
        return result ^ (value + 9);
    }
}

[[runtime_only, noinline, link_name("ifconv_sign_boundary")]]
global u64 ifconv_sign_boundary(in u64 value, in u64 truth,
                                in u64 falsity) {
    return value < 9223372036854775808u64 ? truth : falsity;
}

[[runtime_only, noinline, link_name("ifconv_shared_condition")]]
global u64 ifconv_shared_condition(in u64 left, in u64 right,
                                   in u64 first_truth,
                                   in u64 first_falsity,
                                   in u64 second_truth,
                                   in u64 second_falsity) {
    bool choose = left < right;
    u64 first = choose ? first_truth : first_falsity;
    u64 second = choose ? second_truth : second_falsity;
    return first ^ second;
}

global i32 if_conversion_entry() {
    u64 odd = 5;
    u64 even = 4;
    return (ifconv_compare(1, 2, 10, 20) == 13) +
           (ifconv_compare(3, 2, 10, 20) == 15) +
           (ifconv_return(3, 2, 10, 20) == 13) +
           (ifconv_return(1, 2, 10, 20) == 29) +
           (ifconv_small(1, 250, 7) == 250) +
           (ifconv_small(0, 250, 7) == 7) +
           (ifconv_expensive(1, 2, 3) == 22) +
           (ifconv_expensive(0, 2, 3) == 43) +
           (ifconv_masked_load(&odd, 7) == 23) +
           (ifconv_masked_load(&even, 7) == 10) +
           (ifconv_sign_boundary(7, 11, 13) == 11) +
           (ifconv_sign_boundary(9223372036854775808u64, 11, 13) == 13) +
           (ifconv_shared_condition(1, 2, 10, 20, 5, 7) == 15) +
           (ifconv_shared_condition(2, 1, 10, 20, 5, 7) == 19);
}
