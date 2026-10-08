# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# During translation-time evaluation a [[musttail]] return releases the
# caller's frame, so tail-call chains do not count against the depth limit.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
                            "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(err "${out}${err}" PARENT_SCOPE)
endfunction()

compile(chains [=[
static u64 count(in u64 n, in u64 acc) {
    if (n == 0u64) return acc;
    [[musttail]] return count(n - 1u64, acc + 1u64);
}
[[eval_only]] static u64 ping(in u64 n, in u64 flips) {
    if (n == 0u64) return flips;
    [[musttail]] return ping(n - 1u64, flips ^ 1u64);
}
global u64 counted = count(20000u64, 0u64);
global u64 parity = ping(5001u64, 0u64);
]=])
if(NOT status EQUAL 0)
    message(FATAL_ERROR "tail-call chains failed to evaluate\n${err}")
endif()
file(READ "${OUTPUT}/chains.s" assembly)
if(NOT assembly MATCHES "counted:\n[ \t]*\\.quad 20000\n" OR
   NOT assembly MATCHES "parity:\n[ \t]*\\.quad 1\n")
    message(FATAL_ERROR "tail-call chains produced wrong values\n${assembly}")
endif()

compile(ordinary_depth [=[
static u64 count(in u64 n) { return n == 0u64 ? 0u64 : count(n - 1u64) + 1u64; }
global u64 counted = count(1000u64);
]=])
if(status EQUAL 0 OR NOT err MATCHES "recursion depth exceeded")
    message(FATAL_ERROR "ordinary recursion ignored the depth limit\n${err}")
endif()

compile(released_frame [=[
static u32 read(in const u32 *pointer) { return *pointer; }
static u32 escape() { u32 local = 5u32; [[musttail]] return read(&local); }
global u32 escaped = escape();
]=])
if(status EQUAL 0 OR NOT err MATCHES "outside its lifetime")
    message(FATAL_ERROR "a tail call kept the released frame alive\n${err}")
endif()
