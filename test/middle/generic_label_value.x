// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global void generic_label_owner() {
    global label target:
    ;
}

namespace generic_labels {
    global void other_owner() {
        global label target:
        ;
    }
}

[[generic(label L), noinline]]
#ifdef TEST_MIPS
static label label_identity() {
#else
static label label_identity() -> "r8" {
#endif
    return L;
}

[[noinline]]
#ifdef TEST_MIPS
static label label_echo(in label value) {
#else
static label label_echo(in label value "r10") -> "stack+32" {
#endif
    return value;
}

global u64 generic_label_value_entry() {
    label first = label_identity::<generic_label_owner::target>();
    label second = label_identity::<((generic_label_owner::target))>();
    label other = label_identity::<generic_labels::other_owner::target>();
    label through_stack = label_echo(first);
    return first == second && first == generic_label_owner::target &&
           other == generic_labels::other_owner::target && first != other &&
           through_stack == first;
}
