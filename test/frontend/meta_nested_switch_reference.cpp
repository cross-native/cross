// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Independent host-compiler reference for the nested-switch evaluator fixture.
#include <cstdio>

#ifndef CROSS_ENTRY
#error "CROSS_ENTRY must name the Cross function under test"
#endif
extern "C" int CROSS_ENTRY();

static unsigned branches(unsigned selector) {
    unsigned value = 0, tests = 0;
    switch (selector) {
        if (++tests) { case 1: value += 11; }
        else { case 2: value += 23; }
        value += 5;
        break;
        default: value = 47;
    }
    return value + tests * 100;
}
static unsigned while_entry() {
    unsigned index = 0, tests = 0, total = 0, prefix = 0;
    switch (1) {
        while (++tests < 3) {
            ++prefix;
            case 1:
            total += ++index;
            if (index == 1) continue;
            if (index == 3) break;
        }
        total += 100;
        break;
        default: return 0;
    }
    return total * 1000 + tests * 100 + prefix * 10 + index;
}
static unsigned do_entry() {
    unsigned index = 0, tests = 0, total = 0;
    switch (1) {
        do {
            total += 100;
            case 1:
            total += ++index;
            if (index < 2) continue;
        } while (++tests < 3);
        total += 7;
        break;
        default: return 0;
    }
    return total * 100 + tests * 10 + index;
}
static unsigned for_entry() {
    unsigned index = 0, tests = 0, total = 0, prefix = 0, initial = 0;
    switch (1) {
        for (++initial; ++tests && index < 3; ++index) {
            ++prefix;
            case 1:
            total += index + 1;
            if (index == 0) continue;
        }
        total += 7;
        break;
        default: return 0;
    }
    return total * 10000 + initial * 1000 + tests * 100 + prefix * 10 + index;
}
static unsigned ownership() {
    unsigned total = 0;
    for (unsigned outer = 0; outer < 3; ++outer) {
        switch (outer) {
            if (0) { case 0: total += 1; continue; }
            do { case 1: total += 3; break; } while (1);
            total += 5;
            break;
            default:
                switch (0) { case 0: total += 7; break; }
                total += 11;
                break;
        }
        total += 13;
    }
    return total;
}
static unsigned returns(unsigned selector) {
    switch (selector) {
        for (;;) { if (0) { case 1: { unsigned value = 17; return value; } } }
        default: return 19;
    }
}
static unsigned default_entry() {
    unsigned value = 0;
    switch (9) {
        if (0) { default: value = 17; }
        value += 2;
    }
    return value;
}
int main() {
    if (branches(1) != 16 || branches(2) != 28 || branches(3) != 47 ||
        while_entry() != 106223 || do_entry() != 21333 || for_entry() != 130323 ||
        ownership() != 53 || returns(1) != 17 || returns(2) != 19 || default_entry() != 19) {
        std::fputs("nested switch reference disagrees with expected results\n", stderr);
        return 1;
    }
    return CROSS_ENTRY();
}
