// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "driver/driver.hpp"

#include <cstddef>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace cross {
namespace {

// Deeply nested source recurses through parsing and semantic analysis, so the
// compiler runs on a thread whose stack is far larger than a platform default.
constexpr std::size_t compiler_stack_bytes =
    sizeof(void*) >= 8 ? std::size_t{512} << 20 : std::size_t{64} << 20;

struct EntryCall {
    int (*entry)(int, char**);
    int argc;
    char** argv;
    int result;
};

} // namespace

int run_with_compiler_stack(int (*entry)(int, char**), int argc, char** argv) {
    EntryCall call{entry, argc, argv, 1};
#if defined(_WIN32)
    const HANDLE thread = CreateThread(nullptr, compiler_stack_bytes,
        [](LPVOID data) -> DWORD {
            auto& request = *static_cast<EntryCall*>(data);
            request.result = request.entry(request.argc, request.argv);
            return 0;
        },
        &call, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread) return entry(argc, argv);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
#else
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return entry(argc, argv);
    pthread_attr_setstacksize(&attributes, compiler_stack_bytes);
    pthread_t thread;
    const bool started = pthread_create(&thread, &attributes,
        [](void* data) -> void* {
            auto& request = *static_cast<EntryCall*>(data);
            request.result = request.entry(request.argc, request.argv);
            return nullptr;
        },
        &call) == 0;
    pthread_attr_destroy(&attributes);
    if (!started) return entry(argc, argv);
    pthread_join(thread, nullptr);
#endif
    return call.result;
}

} // namespace cross
