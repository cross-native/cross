// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <initializer_list>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

extern "C" {
extern std::uintptr_t patch_copy_sink;
extern std::uintptr_t patch_branch_sink;
extern std::uintptr_t patch_loop_sink;
extern std::uintptr_t patch_index_sinks[2][2];
extern std::uint32_t patch_expected;
std::uint32_t syntax_raw_entry();
}

static bool patch(std::uintptr_t address, std::uint32_t value) {
    if (!address) return false;
    auto* cell = reinterpret_cast<void*>(address);
    std::uint32_t before{};
    std::memcpy(&before, cell, sizeof(before));
    if (before != 7) return false;
#if defined(_WIN32)
    DWORD previous{};
    if (!VirtualProtect(cell, sizeof(value), PAGE_EXECUTE_READWRITE, &previous)) return false;
    std::memcpy(cell, &value, sizeof(value));
    FlushInstructionCache(GetCurrentProcess(), cell, sizeof(value));
    DWORD discarded{};
    return VirtualProtect(cell, sizeof(value), previous, &discarded) != 0;
#else
    const auto page_size = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    if (!page_size || (page_size & (page_size - 1))) return false;
    const auto page = address & ~(page_size - 1);
    const auto extent = ((address + sizeof(value) + page_size - 1) & ~(page_size - 1)) - page;
    if (mprotect(reinterpret_cast<void*>(page), extent, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;
    std::memcpy(cell, &value, sizeof(value));
    __builtin___clear_cache(static_cast<char*>(cell), static_cast<char*>(cell) + sizeof(value));
    return mprotect(reinterpret_cast<void*>(page), extent, PROT_READ | PROT_EXEC) == 0;
#endif
}

int main() {
    // Obey Cross's immutable patch protocol: patch all fields before any
    // execution of their containing functions, using an independent host writer.
    for (const auto sink : {patch_copy_sink, patch_branch_sink, patch_loop_sink, patch_index_sinks[1][1]}) {
        if (!patch(sink, 23)) {
            std::fputs("could not rewrite the original patch cell\n", stderr);
            return 1;
        }
    }
    patch_expected = 23;
    return static_cast<int>(syntax_raw_entry());
}
