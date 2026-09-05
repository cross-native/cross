// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROSS_ENTRY
#error "CROSS_ENTRY must name the Cross function under test"
#endif

extern "C" int CROSS_ENTRY(int);
extern "C" int dense_unreachable(int);
extern "C" int no_default(int);
extern "C" int single_case(int);
extern "C" int selector_once();
extern "C" int evaluated_switch();
extern "C" int conditional_assume(unsigned, unsigned);

int main() {
    for (int op = -2; op <= 12; ++op) {
        const int dense = op >= 0 && op < 10
            ? (op % 2 == 0 ? op + 11 : op * 3) : -1;
        const int dispatch = op == 0 ? 13 : op == 1 ? 2 :
            op == 2 ? 12 : op == 3 ? 9 : 40;
        const int assumed = op >= 0 && op < 10 ? dense : 0;
        const int loop = op == 11 ? 0 : 3;
        const int expected = dispatch + dense + assumed + 20 + loop;
        if (CROSS_ENTRY(op) != expected) return 1;
        if (op >= 0 && op < 10 && dense_unreachable(op) != dense) return 2;
        if (no_default(op) != (op == 4 ? 44 : 0)) return 3;
        if (single_case(op) != (op == 1 ? 71 : 0)) return 4;
        if (conditional_assume(static_cast<unsigned>(op), 0) !=
            (op >= 0 && op < 10 ? dense : -3)) return 5;
    }
    if (selector_once() != 10 || selector_once() != 2) return 6;
    if (evaluated_switch() != 58) return 7;
    return 0;
}
