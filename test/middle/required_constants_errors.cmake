# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
function(reject name source expected)
    set(path "${OUTPUT}/${name}.x")
    file(WRITE "${path}" "${source}\n")
    execute_process(COMMAND "${CC}" -S -O0 -fno-eval-calls ${ARGN} "${path}" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE s OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(s EQUAL 0 OR NOT err MATCHES "${expected}" OR NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "missing located diagnostic for ${name}: ${expected}\n${out}\n${err}")
    endif()
endfunction()

reject(divzero "global i32 value = 9 / 0;" "division by zero")
reject(overflow "global i32 value = 2147483647i32 + 1;" "signed overflow")
reject(shift "global u32 value = 1u32 << 32;" "invalid shift count")
reject(literal "global u32 value = 4294967296u32;" "integer literal is not representable")
reject(unknown "global i32 value = 0 && missing;" "unresolved name")
reject(arity "static i32 helper(in i32 x) { return x; } global i32 value = 1 || helper();"
    "invalid argument count")
reject(argument "static i32 helper(in i32 x) { return x; } global i32 value = 1 || helper(\"bad\");"
    "incompatible argument type")
reject(pointer_argument "static i32 helper(in const u32 *x) { return 7; } global i32 value = 1 || helper(\"bad\");"
    "incompatible argument type")
reject(dereference "global i32 value = 0 && *1;" "unsupported unary operand")
reject(operand "global i32 value = 0 && (\"bad\" + 1);" "unsupported type|non-integer operation")
reject(conditional "global i32 value = 1 ? 7 : \"bad\";" "unsupported type")
reject(runtime "global i32 value = $::runtime(7);" "runtime is invalid")
reject(runtime_short "global i32 value = 1 || $::runtime(7);" "runtime is invalid")
reject(runtime_only "[[runtime_only]] static i32 seed() { return 1; } global i32 value = seed();"
    "runtime-only function")
reject(runtime_chain "[[runtime_only]] static i32 seed() { return 1; } static i32 helper() { return seed(); } global i32 value = helper();"
    "while evaluating call to 'helper'")
reject(static_read "global i32 state = 7; global i32 value = state;" "not a translation-time value")
reject(unsigned "[[generic(u8 N)]] static i32 g() { return N; } global i32 f() { return g::<-1>(); }"
    "not representable in parameter type 'u8'")
reject(narrow "[[generic(i8 N)]] static i32 g() { return N; } global i32 f() { return g::<128>(); }"
    "not representable in parameter type 'i8'")
reject(bool "[[generic(bool N)]] static i32 g() { return N; } global i32 f() { return g::<2>(); }"
    "not representable in parameter type 'bool'")
reject(generic_divzero "[[generic(i32 N)]] static i32 g() { return N; } global i32 f() { return g::<9 / 0>(); }"
    "division by zero")
reject(generic_unknown "[[generic(i32 N)]] static i32 g() { return N; } global i32 f() { return g::<1 ? 7 : missing>(); }"
    "unresolved name")
reject(generic_runtime "[[generic(i32 N)]] static i32 g() { return N; } global i32 f(in i32 x) { return g::<x>(); }"
    "not a translation-time value")
reject(generic_const_write "[[generic(i32 N)]] static i32 g() { return N; } global i32 f(in const i32 x) { return g::<1 || ++x>(); }"
    "cannot write a const cell")
reject(budget "[[generic(uptr N)]] static i32 g() { return g::<N + 1>(); } global i32 f() { return g::<0>(); }"
    "generic instantiation budget exceeded")

set(pointer_source "[[generic(uptr N)]] static uptr g() { return N; } global uptr f() { return g::<1u64 << 32>(); }")
reject(pointer32 "${pointer_source}" "not representable in parameter type 'uptr'" -mprofile=vr4300-o32)
file(WRITE "${OUTPUT}/pointer64.x" "${pointer_source}\n")
foreach(profile mips64-n64 mips64el-n64)
    execute_process(COMMAND "${CC}" -S -O0 -fno-eval-calls -mprofile=${profile}
        "${OUTPUT}/pointer64.x" -o "${OUTPUT}/${profile}.s"
        RESULT_VARIABLE s OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT s EQUAL 0)
        message(FATAL_ERROR "64-bit generic value rejected for ${profile}\n${out}\n${err}")
    endif()
endforeach()
