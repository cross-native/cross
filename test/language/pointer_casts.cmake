# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# An explicit cast converts between any object pointer types and may discard
# qualifiers; implicit conversions stay strict, and function and object
# pointers stay distinct. Runtime code, static initializers, and
# translation-time evaluation agree.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}\n")
    if(NOT ARGN)
        set(ARGN -target x86_64-unknown-linux-gnu)
    endif()
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(diagnostics "${out}${err}" PARENT_SCOPE)
endfunction()

function(reject name expected source)
    compile(${name} "${source}" ${ARGN})
    string(FIND "${diagnostics}" "error: ${expected}" position)
    if(status EQUAL 0 OR position EQUAL -1 OR
       NOT diagnostics MATCHES "${name}\\.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${name}: missing '${expected}'\n${diagnostics}")
    endif()
endfunction()

function(accept name source)
    compile(${name} "${source}" ${ARGN})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: unexpected rejection\n${diagnostics}")
    endif()
endfunction()

# C idioms: freeing through a const pointer, a type-erased realloc helper,
# and an unaligned word view, plus volatile and nested qualifiers.
set(casts [=[
typedef u32 U32;
[[abi("sysv_abi")]] void free(void *p);
global void release(const u8 *p) { free((void *)p); }
global void **erase(u8 **pp) { return (void **)pp; }
global U32 *words(u8 *p) { return (U32 *)p; }
global u8 *writable(const u8 *p) { return (u8 *)p; }
global u32 *plain(volatile u32 *p) { return (u32 *)p; }
global const u8 **deeper(u8 **pp) { return (const u8 **)pp; }
global u32 [[atomic]] *atomic_view(u32 *p) { return (u32 [[atomic]] *)p; }
static const u32 table[2] = {1u32, 2u32};
static u8 raw[8];
global u32 *table_view = (u32 *)&table[1];
global u32 *raw_words = (u32 *)raw;
global void **raw_erased = (void **)&raw_words;
static u32 read_through() {
    u32 cells[2] = {5u32, 6u32};
    const u32 *view = &cells[1];
    return *(u32 *)view;
}
global u32 evaluated = read_through();
$::static_assert(read_through() == 6u32, "a read through a cast-away const pointer");
]=])
accept(casts_x86_64 "${casts}")
string(REPLACE "[[abi(\"sysv_abi\")]] " "" mips_casts "${casts}")
accept(casts_mips "${mips_casts}" -target mips-unknown-elf)
file(READ "${OUTPUT}/casts_x86_64.s" assembly)
if(NOT assembly MATCHES "table_view:\n\t\\.quad [A-Za-z0-9_]*table\\+4\n" OR
   NOT assembly MATCHES "evaluated:\n\t\\.long 6\n")
    message(FATAL_ERROR "cast static initializers are wrong\n${assembly}")
endif()

set(implicit "implicit pointer conversion discards qualifiers or uses incompatible pointee types")
reject(implicit_const "${implicit}" "global u8 *f(const u8 *p) { return p; }")
reject(implicit_unrelated "${implicit}" "global u32 *f(u8 *p) { return p; }")
reject(implicit_nested "${implicit}" "global const u8 **f(u8 **pp) { return pp; }")
set(mixed "explicit cast cannot convert between a function pointer and an object pointer")
reject(function_to_object "${mixed}"
    "typedef u32 (*fn)(in u32 x);\nglobal void *f(in fn value) { return (void *)value; }")
reject(object_to_function "${mixed}"
    "typedef u32 (*fn)(in u32 x);\nglobal fn f(in u32 *value) { return (fn)value; }")
reject(static_function_to_object "global pointer/label initializer is not an address constant"
    "static u32 target(in u32 x) { return x; }\nglobal void *saved = (void *)&target;")
# Translation-time evaluation does not know whether the object itself is
# const, so it refuses a write through a pointer whose const was cast away.
reject(evaluated_write "translation-time evaluation cannot write through a pointer whose const was cast away"
    "static u32 write_through() { u32 cell = 1u32; const u32 *view = &cell; *(u32 *)view = 2u32; return cell; }\nglobal u32 evaluated = \$::eval(write_through());")
