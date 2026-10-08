// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// These are symbols for inline assembly only, not host-C++ ABI calls.
extern "C" void symbol_function_factory();
extern "C" void symbol_object_factory();
extern "C" void symbol_label_factory();

template<void (*Factory)()>
bool preserves_scratch() {
    unsigned preserved;
    // The fixture selects a 64-bit model with a stack result at the caller's
    // stack base, 16-byte call alignment, and preserved RAX. Set up that frame
    // independently of the host ABI. RBX is restored before leaving the asm.
    // Clear the result first, so a missing result store cannot pass this check.
    asm volatile(
        "pushq %%rbx\n\t"
        "movq %%rsp, %%rbx\n\t"
        "andq $-16, %%rsp\n\t"
        "subq $48, %%rsp\n\t"
        "movq $0, (%%rsp)\n\t"
        "movq $61, %%rax\n\t"
        "call %c1\n\t"
        "cmpq $0, (%%rsp)\n\t"
        "setne %%dl\n\t"
        "cmpq $61, %%rax\n\t"
        "sete %%cl\n\t"
        "andb %%dl, %%cl\n\t"
        "movq %%rbx, %%rsp\n\t"
        "popq %%rbx\n\t"
        : "=c"(preserved)
        : "i"(Factory)
        : "rax", "rdx", "r8", "r9", "r10", "r11", "memory", "cc");
    return (preserved & 255u) == 1u;
}

int main() {
    return preserves_scratch<symbol_function_factory>() &&
        preserves_scratch<symbol_object_factory>() &&
        preserves_scratch<symbol_label_factory>() ? 61 : 0;
}
