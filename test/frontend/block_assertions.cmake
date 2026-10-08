# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
function(reject name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
           NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

reject(local_type "static_assert failed: local mismatch" [=[
global u32 entry() { u16 value; $::static_assert(sizeof(value) == 4uptr, "local mismatch"); return 0u32; }
]=])
reject(local_value "constant|runtime|uninitialized" [=[
global u32 entry() { u32 value = 3u32; $::static_assert(value == 3u32, "not a runtime check"); return 0u32; }
]=])
reject(parameter_value "constant|runtime|uninitialized" [=[
static u32 helper(in u32 value) { $::static_assert(value == 3u32, "not invocation-dependent"); return value; }
global u32 entry() { return helper(3u32); }
]=])
reject(unused_ordinary "static_assert failed: unused ordinary" [=[
static u32 unused() { u32 value; $::static_assert(sizeof(value) == 1uptr, "unused ordinary"); return 0u32; }
global u32 entry() { return 0u32; }
]=])
reject(untaken_ordinary "static_assert failed: untaken ordinary" [=[
global u32 entry() { if (0u32) $::static_assert(0u32, "untaken ordinary"); return 0u32; }
]=])
reject(unreachable_ordinary "static_assert failed: unreachable ordinary" [=[
global u32 entry() { return 0u32; $::static_assert(0u32, "unreachable ordinary"); }
]=])
reject(concrete_generic "static_assert failed: generic mismatch" [=[
static T helper<T>(in T value) { $::static_assert(sizeof(value) == 4uptr, "generic mismatch"); return value; }
global u32 entry() { return (u32)helper(3u16); }
]=])
reject(ended_scope "not visible|unknown|unresolved|constant" [=[
global u32 entry() { { u32 value; } $::static_assert(sizeof(value) == 4uptr, "scope ended"); return 0u32; }
]=])
reject(vla_not_constant "constant|layout|size|VLA|variable" [=[
global u32 entry(in uptr count) { u8 values[count]; $::static_assert(sizeof(values) == 3uptr, "dynamic size"); return 0u32; }
]=])
reject(zero_generic_extent "fixed array bound must be a positive integer" [=[
static u32 helper<u32 N>() { u8 values[N]; return 0u32; }
global u32 entry() { return helper<0u32>(); }
]=])
