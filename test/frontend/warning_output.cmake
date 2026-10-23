# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Warnings never change the output: the same source compiled with and without
# every warning, including the shipped format checker, gives the same assembly.
foreach(required CC TARGET OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(WRITE "${OUTPUT}/everything.x" [=[
[[checker]]
static void note_calls(in $::meta::call call) {
    $::meta::warning($::meta::call_span(call), "checked call");
}
[[check(note_calls)]] void sink(in u32 value);
[[check($::format::printf)]] i32 report(in const u8 *format, ...);
static u32 spare() { return 1; }
static u32 relay<T>(in T value) { return (u32)value; }
global u32 entry(in u32 x, in u32 unused, in i32 s) {
    u32 local = x;
    u32 maybe;
    if (s) maybe = x;
    u8 narrow = x;
    bool less = s < x;
    sink(x);
    report("%u %u\n", x);
    switch (x) {
    case 0: x = 1;
    case 1: break;
    }
    return x + narrow + less + relay(s) + maybe;
}
]=])
foreach(level O0 O2)
    foreach(variant quiet loud)
        set(flags -w)
        if(variant STREQUAL loud)
            set(flags -Wall -Wextra -Wconversion)
        endif()
        execute_process(COMMAND "${CC}" -S -${level} -target ${TARGET} ${flags}
            "${OUTPUT}/everything.x" -o "${OUTPUT}/everything-${level}-${variant}.s"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT result EQUAL 0 OR (variant STREQUAL loud AND NOT error MATCHES "warning:"))
            message(FATAL_ERROR "${level} ${variant}: unexpected result ${result}\n${error}")
        endif()
    endforeach()
    file(READ "${OUTPUT}/everything-${level}-quiet.s" quiet)
    file(READ "${OUTPUT}/everything-${level}-loud.s" loud)
    if(NOT quiet STREQUAL loud)
        message(FATAL_ERROR "warnings changed the ${TARGET} -${level} output")
    endif()
endforeach()
