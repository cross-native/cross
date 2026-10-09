// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Calls each function of hard_register_unwind.x through a trampoline that
// loads canaries into preserved registers. The probe the functions call
// unwinds its own frame and the Cross frame with the Windows unwinder and
// checks that every canary is restored.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

using U64 = std::uint64_t;

extern "C" {
__attribute__((sysv_abi)) U64 hard_unwind_call(void (*function)(), U64 count);
void hard_unwind_fixed();
void hard_unwind_win64();
void hard_unwind_dynamic();
void hard_unwind_realigned();
void hard_unwind_realigned_dynamic();
void hard_unwind_large();
}

namespace {

CONTEXT unwound;
int probes;
int failures;

void expect(const char* name, const char* value, U64 actual, U64 expected) {
    if (actual == expected) return;
    std::fprintf(stderr, "%s: %s is %llx, expected %llx\n", name, value,
                 static_cast<unsigned long long>(actual),
                 static_cast<unsigned long long>(expected));
    ++failures;
}

U64 low_lane(const M128A& lane) {
    U64 value;
    std::memcpy(&value, &lane, sizeof value);
    return value;
}

void run(const char* name, void (*function)(), U64 result, bool win64) {
    probes = 0;
    expect(name, "result", hard_unwind_call(function, 5), result);
    expect(name, "probe count", static_cast<U64>(probes), 1);
    if (probes != 1) return;
    expect(name, "rbx", unwound.Rbx, 0x0101010101010101ULL);
    expect(name, "r12", unwound.R12, 0x0303030303030303ULL);
    expect(name, "r13", unwound.R13, 0x0404040404040404ULL);
    expect(name, "r14", unwound.R14, 0x0505050505050505ULL);
    expect(name, "r15", unwound.R15, 0x0606060606060606ULL);
    if (!win64) return;
    expect(name, "rsi", unwound.Rsi, 0x0707070707070707ULL);
    expect(name, "rdi", unwound.Rdi, 0x0808080808080808ULL);
    expect(name, "xmm6", low_lane(unwound.Xmm6), 0x1010101010101010ULL);
    expect(name, "xmm15", low_lane(unwound.Xmm15), 0x1010101010101010ULL);
}

} // namespace

extern "C" __attribute__((noinline)) void hard_unwind_probe() {
    CONTEXT context;
    RtlCaptureContext(&context);
    // Unwind this probe's frame, then the Cross function's frame.
    for (int frame = 0; frame < 2; ++frame) {
        DWORD64 base = 0;
        auto* entry = RtlLookupFunctionEntry(context.Rip, &base, nullptr);
        if (!entry) {
            std::fprintf(stderr, "frame %d has no unwind entry\n", frame);
            ++failures;
            return;
        }
        void* handler_data = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context.Rip, entry,
                         &context, &handler_data, &establisher, nullptr);
    }
    unwound = context;
    ++probes;
}

int main() {
    run("fixed", hard_unwind_fixed, 23, false);
    run("win64", hard_unwind_win64, 46, true);
    run("dynamic", hard_unwind_dynamic, 34, false);
    run("realigned", hard_unwind_realigned, 42, false);
    run("realigned_dynamic", hard_unwind_realigned_dynamic, 54, false);
    run("large", hard_unwind_large, 64, false);
    return failures == 0 ? 0 : 1;
}
