// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct pair { u64 first; u64 second; };

[[abi("example-regs")]]
global struct pair make(in u64 a, in u64 b) {
    struct pair result;
    result.first = a;
    result.second = b;
    return result;
}

[[abi("example-regs")]]
global struct pair make_external(in u64 a);

global u64 call_external() {
    return make_external(1).first;
}

global u64 call_pointer(struct pair (*maker)(in u64 a) [[abi("example-regs")]]) {
    return maker(2).first;
}

[[abi("regs-only")]]
global u64 take(in u64 a, in struct pair value) {
    return value.first + a;
}

[[abi("regs-only")]]
global u64 take_external(in struct pair value, in u64 b);

global u64 call_take(struct pair value) {
    return take_external(value, 3);
}
