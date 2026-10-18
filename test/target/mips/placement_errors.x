// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

struct pair { u32 first; u32 second; };

[[abi("mips-regs")]]
global struct pair make(in u32 a, in u32 b) {
    struct pair result;
    result.first = a;
    result.second = b;
    return result;
}

[[abi("mips-regs")]]
global struct pair make_external(in u32 a);

global u32 call_external() {
    return make_external(1u32).first;
}

global u32 call_pointer(struct pair (*maker)(in u32 a) [[abi("mips-regs")]]) {
    return maker(2u32).first;
}

[[abi("mips-regs-only")]]
global u32 take(in u32 a, in struct pair value) {
    return value.first + a;
}

[[abi("mips-regs-only")]]
global u32 take_external(in struct pair value, in u32 b);

global u32 call_take(struct pair value) {
    return take_external(value, 3u32);
}
